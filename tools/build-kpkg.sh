#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the KPM package from build/ (run scripts/build-macos.sh first).
# KPM reads manifest.json at the archive root, so files are not nested.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
version=$(python3 -c 'import json;print(".".join(map(str,json.load(open("kpm/manifest.json"))["version"])))')
binary=$(sed -n 's/^#define FW_VERSION "\([^"]*\)"$/\1/p' src/core.h)
sync=$(sed -n 's/^#define SYNC_VERSION "\([^"]*\)"$/\1/p' src/sync.c)
[ "$version" = "$binary" ] && [ "$version" = "$sync" ] || { echo "version mismatch: manifest=$version app=$binary sync=$sync" >&2; exit 1; }
stage="dist/fw-$version"
out="packages/fw/artifacts/fw_${version}_kindlehf.kpkg"
[ ! -e "$out" ] || { echo "$out exists; published versions are immutable" >&2; exit 1; }
rm -rf "$stage"; mkdir -p "$stage/bin" "$stage/scriptlets" "$(dirname "$out")"
cp build/financewiki build/financewiki-sync "$stage/bin/"
cp launch.sh kpm/install.sh kpm/uninstall.sh kpm/manifest.json "$stage/"
cp kpm/FinanceWiki.sh kpm/FinanceWiki_Update.sh "$stage/scriptlets/"
mkdir -p "$stage/fonts"
cp fonts/NotoSerifCJKkr-SemiBold.ttf fonts/NotoSansCJKkr-Medium.ttf fonts/NotoSansCJKkr-Bold.otf "$stage/fonts/"
cp -R licenses "$stage/"
cp LICENSE THIRD_PARTY.md "$stage/"
chmod 755 "$stage"/*.sh "$stage"/bin/*
find "$stage" -type d -exec chmod 755 {} +
COPYFILE_DISABLE=1 tar --no-xattrs --no-mac-metadata --uid 0 --gid 0 -C "$stage" -czf "$out" \
    manifest.json install.sh uninstall.sh launch.sh scriptlets bin fonts licenses LICENSE THIRD_PARTY.md
printf '%s\n' "$out"
