# Third-party source and licenses

Application code: GPL-3.0-or-later; full license in LICENSE. FinanceWiki Markdown/data is not included and is not licensed by this project.

| Component | Pinned source | Terms / location |
|---|---|---|
| FBInk | https://github.com/NiLuJe/FBInk/commit/886f25f13368859ad8a899b88d04c26e19cda32e | GPL-3.0-or-later; vendor/FBInk/LICENSE |
| stb (FBInk fork) | https://github.com/NiLuJe/stb/commit/dfa32facac4a16184dd66f8e482bf15a678f2bf3 | MIT option; vendor/FBInk/stb/LICENSE |
| libunibreak | https://github.com/adah1972/libunibreak/commit/3ce4bfa3129ff3738046a44a6db533d2ce25af2b | zlib-style license in source headers |
| cutef8 | included in pinned FBInk | Original copyright/terms retained in source headers |
| QImageScale | included in pinned FBInk | vendor/FBInk/qimagescale/QIMAGETRANSFORM_LICENSE.txt and source headers |
| Noto Sans CJK KR Regular/Bold | https://github.com/notofonts/noto-cjk/tree/main/Sans/OTF/Korean | SIL OFL 1.1; licenses/OFL-Noto-CJK.txt; exact font hashes in dependencies.json |
| musl libc | bundled in Zig 0.16.0, statically linked | MIT; licenses/musl-COPYRIGHT.txt |
| SQLite | SQLite 3.50.2 amalgamation, vendored in `vendor/sqlite/` | Public domain; https://sqlite.org/copyright.html |

KUAL Next informed the temporary Pillow/Awesome/statusbar lifecycle, SH Integration metadata and panel coordinate handling:
https://github.com/thiagokokada/kual-next/commit/fb36257856a9bb036b32eed30761c654d1e52f5b
KUAL Next Copyright (C) 2026 KUAL Next contributors, GPL-3.0-or-later.
Its application/menu engine is not included or required. Relevant attribution is retained here.

Local FBInk change: in fbink_state_dump's non-Kobo branch, add the missing `isTolino` false argument to match the printf format. This removes format/argument mismatch warnings. No device IO behavior is changed. See vendor/FBInk/fbink.c.

This source distribution includes FBInk and the required submodule sources. Unused font8x8, libevdev and i2c-tools submodules are omitted; the supplied Makefile does not build them. No network fetch occurs during `make`.

Keep the accompanying source archive with the binary package when redistributing; it contains the complete corresponding application/library source and build instructions. Compiler toolchains are obtained separately from their upstream distribution.
