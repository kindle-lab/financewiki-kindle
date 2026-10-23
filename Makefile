# SPDX-License-Identifier: GPL-3.0-or-later
# CC may be the kindlehf cross compiler or `zig cc -target arm-linux-musleabihf -mcpu=cortex_a7`.
HOST_CC ?= cc
FB = vendor/FBInk
CPPFLAGS += -Isrc -I$(FB) -Ivendor/sqlite -D_GNU_SOURCE -DFBINK_FOR_KINDLE -DFBINK_MINIMAL -DFBINK_WITH_DRAW -DFBINK_WITH_OPENTYPE -DFBINK_WITH_IMAGE -DFBINK_WITH_INPUT -DFBINK_VERSION='"886f25f-phase4"' -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_ENABLE_FTS5
CFLAGS ?= -O2 -std=gnu11
FB_SRC = $(FB)/fbink.c $(FB)/cutef8/utf8.c $(FB)/cutef8/dfa.c $(FB)/qimagescale/qimagescale.c
UB_SRC = $(addprefix $(FB)/libunibreak/src/,linebreak.c linebreakdata.c unibreakdef.c linebreakdef.c eastasianwidthdef.c)
SQLITE_SRC = vendor/sqlite/sqlite3.c
all: build/financewiki build/financewiki-sync
build:
	mkdir -p build
build/financewiki: src/main.c src/core.c src/process.c src/snapshot.c src/markdown.c src/layout.c src/core.h src/process.h src/snapshot.h src/markdown.h src/layout.h $(FB_SRC) $(UB_SRC) $(SQLITE_SRC) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) src/main.c src/core.c src/process.c src/snapshot.c src/markdown.c src/layout.c $(FB_SRC) $(UB_SRC) $(SQLITE_SRC) -lm $(LDFLAGS) -o $@
build/financewiki-sync: src/sync.c src/index.c src/index.h $(SQLITE_SRC) | build
	$(CC) -Isrc -Ivendor/sqlite -D_GNU_SOURCE -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION $(CFLAGS) src/sync.c src/index.c $(SQLITE_SRC) -lm $(LDFLAGS) -o $@
build/core-test: tests/core_test.c src/core.c src/core.h | build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Isrc tests/core_test.c src/core.c -o $@
build/process-test: tests/process_test.c src/process.c src/process.h | build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Isrc tests/process_test.c src/process.c -o $@
build/font-check: tests/font_check.c src/core.c src/core.h | build
	$(HOST_CC) -std=c11 -O2 -Isrc -I$(FB)/stb tests/font_check.c src/core.c -lm -o $@
build/snapshot-test: tests/snapshot_test.c src/snapshot.c src/snapshot.h $(SQLITE_SRC) | build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Isrc -Ivendor/sqlite -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_ENABLE_FTS5 tests/snapshot_test.c src/snapshot.c $(SQLITE_SRC) -lm -o $@
build/markdown-test: tests/markdown_test.c src/markdown.c src/markdown.h | build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -D_GNU_SOURCE -Isrc tests/markdown_test.c src/markdown.c -o $@
build/layout-test: tests/layout_test.c src/layout.c src/layout.h src/markdown.c src/markdown.h | build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -D_GNU_SOURCE -Isrc tests/layout_test.c src/layout.c src/markdown.c -o $@
build/index-test: tests/index_test.c src/index.c src/index.h | build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Isrc tests/index_test.c src/index.c -o $@
build/financewiki-sync-test: src/sync.c src/index.c src/index.h $(SQLITE_SRC) | build
	$(HOST_CC) -Isrc -Ivendor/sqlite -D_GNU_SOURCE -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -O2 -std=gnu11 src/sync.c src/index.c $(SQLITE_SRC) -lm -o $@
build/sync-test: build/financewiki-sync-test tests/sync_test.sh tests/mock_curl.sh | build
	FW_SYNC_BINARY=./build/financewiki-sync-test sh tests/sync_test.sh
test: build/core-test build/process-test build/font-check build/snapshot-test build/markdown-test build/index-test build/layout-test build/sync-test
	./build/core-test
	./build/process-test
	./build/font-check fonts/NotoSerifCJKkr-SemiBold.ttf
	./build/font-check fonts/NotoSansCJKkr-Medium.ttf
	./build/font-check fonts/NotoSansCJKkr-Bold.otf
	./build/snapshot-test
	./build/markdown-test
	./build/index-test
	./build/layout-test
	sh -n launch.sh
	sh -n kpm/install.sh
	sh -n kpm/uninstall.sh
.PHONY: all test
