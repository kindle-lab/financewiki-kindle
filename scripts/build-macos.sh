#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# No installs outside the project. Downloads only the official compiler.
set -eu
cd "$(dirname "$0")/.."
[ "$(uname -s)" = Darwin ] && [ "$(uname -m)" = arm64 ] || {
    printf '%s\n' 'This helper is for Apple Silicon macOS. See README for Linux.' >&2; exit 1;
}
mkdir -p .cache
compiler=.cache/zig-aarch64-macos-0.16.0
if [ ! -x "$compiler/zig" ]; then
    curl --fail --location --proto '=https' --tlsv1.2 \
      https://ziglang.org/download/0.16.0/zig-aarch64-macos-0.16.0.tar.xz -o .cache/zig.tar.xz
    actual=$(shasum -a 256 .cache/zig.tar.xz | awk '{print $1}')
    [ "$actual" = b23d70deaa879b5c2d486ed3316f7eaa53e84acf6fc9cc747de152450d401489 ] || {
        printf '%s\n' 'Zig checksum mismatch.' >&2; exit 1;
    }
    tar -xf .cache/zig.tar.xz -C .cache
fi
ZIG_GLOBAL_CACHE_DIR="$PWD/.cache/zig-cache"; export ZIG_GLOBAL_CACHE_DIR
rm -f build/financewiki build/financewiki-sync
make CC="$compiler/zig cc -target arm-linux-musleabihf -mcpu=cortex_a7" LDFLAGS='-static -s'
make test
node tests/launcher_test.mjs
node scripts/check-elf.mjs build/financewiki
node scripts/check-elf.mjs build/financewiki-sync
