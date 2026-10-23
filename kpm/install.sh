#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# KPM runs this as: sh install.sh [upgrade], from the package directory.
set -eu
self=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
data=/var/local/financewiki-kindle
if ! mkdir -p "$data" 2>/dev/null || ! : >"$data/.write-test" 2>/dev/null; then
    data=/mnt/us/financewiki-kindle
    mkdir -p "$data"
fi
rm -f "$data/.write-test"
chmod 700 "$self/launch.sh" "$self/uninstall.sh" "$self/bin/financewiki" "$self/bin/financewiki-sync"
chmod 700 "$data"

# Library entries. Rewriting an unchanged scriptlet makes the Kindle library
# drop it, so copy only when the content differs.
mkdir -p /mnt/us/documents
for entry in FinanceWiki.sh FinanceWiki_Update.sh; do
    if ! cmp -s "$self/scriptlets/$entry" "/mnt/us/documents/$entry"; then
        cp "$self/scriptlets/$entry" "/mnt/us/documents/$entry"
    fi
done

# GitHub token (read-only PAT), stored once in source.conf (mode 0600), never printed.
# Sources, first match wins: an existing source.conf, the 0.7.x token file, or a
# one-line token file dropped at documents/FinanceWikiGitHubToken over USB.
conf="$data/source.conf"
if [ ! -s "$conf" ]; then
    for candidate in /mnt/us/financewiki-kindle/config/github-token /mnt/us/documents/FinanceWikiGitHubToken; do
        [ -s "$candidate" ] || continue
        token=$(tr -d '\r\n' <"$candidate")
        [ -n "$token" ] || continue
        umask 077
        printf 'owner=financewiki-park\nrepository=financewiki-private\nbranch=main\ntoken=%s\n' "$token" >"$conf.tmp"
        chmod 600 "$conf.tmp"
        mv "$conf.tmp" "$conf"
        [ "$candidate" = /mnt/us/documents/FinanceWikiGitHubToken ] && rm -f "$candidate"
        printf '%s\n' 'GitHub token configured.'
        break
    done
fi
[ -s "$conf" ] || printf '%s\n' 'No GitHub token yet: run bin/financewiki-sync --configure in KTerm, or copy a token file to documents/FinanceWikiGitHubToken and reinstall.'
printf '%s\n' "FinanceWiki Kindle installed. data_dir=$data"
exit 0
