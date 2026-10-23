/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Fake monospace measurer: one character = size px wide, a line = size*4/3 px tall. */
typedef struct { int drawn_height_total; int draws; char last[4096]; } Ctx;
static int chars(const char *s) { int n = 0; for (; *s; s++) if (((unsigned char)*s & 0xc0) != 0x80) n++; return n; }
static int lines_for(const char *t, int per_line) {
    int lines = 0; const char *p = t;
    for (;;) { const char *e = strchr(p, '\n'); size_t n = e ? (size_t)(e - p) : strlen(p);
        char buf[4096]; memcpy(buf, p, n); buf[n] = 0; int c = chars(buf); lines += c ? (c + per_line - 1) / per_line : 1;
        if (!e) break; p = e + 1; }
    return lines;
}
static int measure(void *ctx, const char *t, const FWStyle *st, int width, int available, bool *fits, int *height) {
    (void)ctx; int per = width / st->size; if (per < 1) per = 1; int lh = st->size * 4 / 3;
    int need = lines_for(t, per) * lh; *fits = need <= available; *height = *fits ? need : (available / lh) * lh; return 0;
}
static int draw(void *ctx, const char *t, const FWStyle *st, int x, int y, int width, int height) {
    Ctx *c = ctx; (void)st; (void)x; (void)width; assert(y >= 100 && y + height <= 100 + 600); c->draws++; c->drawn_height_total += height;
    snprintf(c->last, sizeof(c->last), "%s", t); return 0;
}
static int rule(void *ctx, int x, int y, int w) { (void)ctx; (void)x; (void)w; assert(y >= 100 && y <= 700); return 0; }
int main(void) {
    Ctx c = {0}; FWRenderer r = { measure, draw, rule, NULL, &c }; FWArea area = { 40, 100, 600, 600 };
    /* 20 chars per line at 30px; 40px lines => 15 lines per page */
    char md[20000] = "# 제목\n\n"; for (int i = 0; i < 40; i++) strcat(md, "가나다라마 바사아자차 카타파하가 나다라마바 ");
    strcat(md, "\n\n- 하나\n  - 둘\n\n| a | b |\n|---|---|\n| x | y |\n\n---\n\n끝 문단.\n");
    MdDocument d = {0}; assert(markdown_parse(md, &d) == 0);
    FWPos pos = {0, 0}, next; int pages = 0;     while (fw_layout_page(&d, pos, &r, area, 30, 1, true, &next) == 0) {
        pages++; assert(pages < 50);
        assert(next.block > pos.block || (next.block == pos.block && next.offset > pos.offset)); /* always progresses */
        pos = next;
    }
    assert(pages >= 4 && pages <= 8);
    assert(!strcmp(c.last, "끝 문단."));
    /* a split never cuts in the middle of a word when a space is near */
    FWPos p1 = {0, 0}, n1; assert(fw_layout_page(&d, p1, &r, area, 30, 1, false, &n1) == 0);
    assert(n1.block == 1 && n1.offset > 0 && d.blocks[1].text[n1.offset - 1] == ' ');
    /* end of document */
    FWPos end = { d.count, 0 }; assert(fw_layout_page(&d, end, &r, area, 30, 1, false, &next) == 1);
    /* a single word longer than a page still progresses */
    MdDocument big = {0}; char word[3000]; memset(word, 'x', 2999); word[2999] = 0; assert(markdown_parse(word, &big) == 0);
    pos = (FWPos){0, 0}; pages = 0; while (fw_layout_page(&big, pos, &r, area, 30, 1, false, &next) == 0) { pages++; assert(pages < 20); pos = next; }
    assert(pages == 10); /* 20 chars x 15 lines = 300 per page */
    markdown_free(&d); markdown_free(&big);
    puts("layout: full pages, word-boundary splits, rules, end of document and oversized words passed");
}
