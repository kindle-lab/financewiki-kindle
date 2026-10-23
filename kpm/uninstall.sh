#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
if [ "${1:-}" = upgrade ]; then
    exit 0
fi
for entry in FinanceWiki.sh FinanceWiki_Update.sh; do
    if [ -f "/mnt/us/documents/$entry" ] && cmp -s "/mnt/us/documents/$entry" "./scriptlets/$entry"; then
        rm -f "/mnt/us/documents/$entry"
    fi
done
printf '%s\n' 'Removed the FinanceWiki Kindle package. Cached documents, settings and the token stay in /var/local/financewiki-kindle (or /mnt/us/financewiki-kindle).'
exit 0
