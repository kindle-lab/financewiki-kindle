#!/bin/sh
# Fake curl for tests/sync_test.sh. Serves files from $FW_SYNC_FIXTURE.
set -eu
out=
url=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --output) out=$2; shift 2 ;;
        --url) url=$2; shift 2 ;;
        *) shift ;;
    esac
done
[ -n "${FW_SYNC_DEBUG:-}" ] && printf '%s\n' "$url" >>"$FW_SYNC_DEBUG"
[ -n "$out" ] && [ -n "$url" ]
case "$url" in
    */commits/*) cp "$FW_SYNC_FIXTURE/sha" "$out" ;;
    *viewer-index.json*) cp "$FW_SYNC_FIXTURE/index.json" "$out" ;;
    *)
        name=${url##*/contents/}; name=${name%%\?*}; name=$(basename "$name")
        if [ -n "${FW_SYNC_FAIL:-}" ] && [ "$name" = "$FW_SYNC_FAIL" ]; then exit 28; fi
        [ -f "$FW_SYNC_FIXTURE/$name" ] || exit 22
        cp "$FW_SYNC_FIXTURE/$name" "$out" ;;
esac
printf '200'
