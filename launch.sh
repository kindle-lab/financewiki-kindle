#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Framework lifecycle approach informed by KUAL Next (see THIRD_PARTY.md).
set -u
cd "$(dirname "$0")" || exit 1
app_dir=$(pwd -P)
binary="$app_dir/bin/financewiki"
sync_binary="$app_dir/bin/financewiki-sync"
# Same rule as install.sh and financewiki-sync: /var/local, else the USB storage.
data_dir=${FINANCEWIKI_DATA_DIR:-}
if [ -z "$data_dir" ]; then
    data_dir=/var/local/financewiki-kindle
    if ! mkdir -p "$data_dir" 2>/dev/null || ! : >"$data_dir/.write-test" 2>/dev/null; then data_dir=/mnt/us/financewiki-kindle; fi
    rm -f "$data_dir/.write-test" 2>/dev/null
fi
lock=/var/tmp/financewiki-phase2.lock
proc=/proc
status=/sbin/status
start=/sbin/start
stop=/sbin/stop
lipc=/usr/bin/lipc-set-prop
pidof=/usr/bin/pidof
power=/usr/bin/lipc-wait-event
# Explicit test harness only: mocks must never run on a Kindle framebuffer.
if [ "${FW_LAUNCH_TEST:-0}" = 1 ] && [ ! -e /dev/fb0 ]; then
    binary=$FW_TEST_BINARY
    sync_binary=$FW_TEST_SYNC_BINARY
    lock=$FW_TEST_LOCK
    proc=$FW_TEST_PROC
    status=$FW_TEST_STATUS
    start=$FW_TEST_START
    stop=$FW_TEST_STOP
    lipc=$FW_TEST_LIPC
    pidof=$FW_TEST_PIDOF
    power=$FW_TEST_POWER
    data_dir=${FW_TEST_DATA_DIR:-"$app_dir/state"}
fi
mkdir -p "$app_dir/state/logs" || exit 1
if ! mkdir "$lock" 2>/dev/null; then
    printf '%s\n' 'FinanceWiki is running, or its previous launcher lock remains.' >&2
    exit 1
fi
awesome_pid=
awesome_owned=0
pillow_owned=0
statusbar_owned=0
screensaver_owned=0
took_screen=0
child=
restore_failed=0
bounded() { "$binary" --bounded-command "$@"; }
restore() {
    if [ "$awesome_owned" = 1 ]; then
        if kill -CONT "$awesome_pid" 2>/dev/null; then awesome_owned=0; else restore_failed=1; fi
    fi
    if [ "$pillow_owned" = 1 ]; then
        if bounded "$lipc" com.lab126.pillow disableEnablePillow enable; then pillow_owned=0; else restore_failed=1; fi
    fi
    if [ "$screensaver_owned" = 1 ]; then
        if bounded "$lipc" com.lab126.powerd preventScreenSaver 0; then screensaver_owned=0; else restore_failed=1; fi
    fi
    if [ "$statusbar_owned" = 1 ]; then
        if bounded "$start" statusbar; then statusbar_owned=0; else restore_failed=1; fi
    fi
}
cleanup() {
    trap '' HUP INT TERM
    if [ -n "$child" ]; then
        kill -TERM "$child" 2>/dev/null || :
        wait "$child" 2>/dev/null || :
    fi
    restore
    # Like the 0.7.0 reader: have the framework redraw Home, so the status bar
    # (clock, battery) comes back instead of a Home screen shifted down a line.
    if [ "$took_screen" = 1 ]; then bounded "$lipc" com.lab126.appmgrd start app://com.lab126.booklet.home || :; fi
    rmdir "$lock" 2>/dev/null || :
    if [ "$restore_failed" = 1 ]; then
        printf '%s\n' 'ERROR: framework restoration needs inspection.' >&2
    fi
}
terminate() { if [ -n "$child" ]; then kill -TERM "$child" 2>/dev/null || :; else exit 143; fi; }
trap cleanup EXIT
trap terminate HUP INT TERM
if [ -f state/logs/session.log ]; then mv state/logs/session.log state/logs/previous.log; fi
exec >>state/logs/session.log 2>&1
printf 'FinanceWiki session %s (local %s)\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$(date '+%Y-%m-%d %H:%M %Z')"
if [ ! -x "$binary" ] || [ ! -x "$sync_binary" ] || [ ! -x "$lipc" ] || [ ! -x "$power" ]; then
    printf '%s\n' 'Required binary/LIPC tool missing.'; exit 1
fi
# Do not take over a screen already owned by KOReader or another native app.
awesome_pid=$(bounded "$pidof" awesome) || exit 1
case "$awesome_pid" in ''|*[!0-9]*) printf '%s\n' 'Expected a single Awesome PID.'; exit 1;; esac
awesome_state=$(awk '/^State:/ {print $2}' "$proc/$awesome_pid/status") || exit 1
case "$awesome_state" in T|t|'') printf '%s\n' 'Kindle UI already paused. Return to Kindle home first.'; exit 1;; esac
if bounded "$status" statusbar 2>/dev/null | grep -q 'start/running'; then
    statusbar_owned=1
    bounded "$stop" statusbar || exit 1
fi
pillow_owned=1
bounded "$lipc" com.lab126.pillow disableEnablePillow disable || exit 1
awesome_owned=1
took_screen=1
kill -STOP "$awesome_pid" || exit 1
# The screen is ours now: sync with a progress screen, then check the cache.
# Network access is optional.  The synchronizer keeps every fetched document
# and falls back to the cache if GitHub, DNS, Wi-Fi, or HTTPS is unavailable.
export FINANCEWIKI_DATA_DIR="$data_dir"
FW_SYNC_PROGRESS=1; export FW_SYNC_PROGRESS
{ "$sync_binary" --auto || printf '%s\n' 'FinanceWiki sync failed; opening the previous cache.' >&2; } | "$binary" --progress
unset FW_SYNC_PROGRESS
"$binary" --probe || exit 1
# Keep the Kindle awake while reading; the reader closes itself after 15 idle minutes.
if bounded "$lipc" com.lab126.powerd preventScreenSaver 1; then screensaver_owned=1; fi
FW_LAUNCHED=1 FW_AWESOME_PID="$awesome_pid" FW_PILLOW_OWNED="$pillow_owned" FW_STATUSBAR_OWNED="$statusbar_owned" FW_SCREENSAVER_OWNED="$screensaver_owned" "$binary" &
child=$!
rc=0
while :; do
    wait "$child"; rc=$?
    kill -0 "$child" 2>/dev/null || break
done
child=
restore
if [ "$restore_failed" = 1 ]; then rc=125; fi
printf 'Launcher exit=%s\n' "$rc"
exit "$rc"
