/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FW_LAYOUT_H
#define FW_LAYOUT_H
#include "markdown.h"
#include <stdbool.h>
#include <stddef.h>
/* Page layout of Markdown blocks: each block is measured against the space
   left on the page and split at a word boundary when it does not fit, so a
   page is always full and never overflows. Measuring and drawing are done by
   the caller (FBInk on the Kindle, a fake in tests). */
/* family: 0 = sans (고딕), 1 = serif (명조). Headings and table rows are bold sans. */
/* underline marks a row that opens a linked document. */
typedef struct { int size; bool bold; int family; int indent; int space_before; bool underline; } FWStyle;
typedef struct {
    /* Height in px of `text` laid out in `width`, limited to `available` px.
       *fits is false when the whole text does not fit in that height. */
    int (*measure)(void *ctx, const char *text, const FWStyle *style, int width, int available, bool *fits, int *height);
    int (*draw)(void *ctx, const char *text, const FWStyle *style, int x, int y, int width, int height);
    int (*rule)(void *ctx, int x, int y, int width);
    /* Optional: called for every block drawn, with its screen rectangle (for taps). */
    int (*hit)(void *ctx, size_t block, int x, int y, int width, int height);
    void *ctx;
} FWRenderer;
typedef struct { int x, y, w, h; } FWArea;
/* Position in a document: block index and byte offset inside its display text. */
typedef struct { size_t block, offset; } FWPos;
FWStyle fw_block_style(const MdBlock *block, int base_size, int body_family);
/* Lays out one page from `start`. With draw=false only measures. Sets *next to
   the first position of the following page. Returns 1 when `start` is already
   the end of the document, 0 on success, -1 on error. */
int fw_layout_page(const MdDocument *doc, FWPos start, const FWRenderer *r, FWArea area,
                   int base_size, int body_family, bool draw, FWPos *next);
#endif
