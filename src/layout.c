/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FWStyle base_style(const MdBlock *b, int s, int f) {
    switch (b->kind) {
    case MD_HEADING:
        if (b->level == 1) return (FWStyle){ s * 5 / 4, true, 0, 0, s * 4 / 5, false };
        if (b->level == 2) return (FWStyle){ s * 9 / 8, true, 0, 0, s * 2 / 3, false };
        return (FWStyle){ s, true, 0, 0, s / 2, false };
    case MD_LIST_ITEM:  return (FWStyle){ s, false, f, s * b->level, s / 5, false };
    case MD_QUOTE:      return (FWStyle){ s, false, f, s * 3 / 4 * b->level, s / 3, false };
    case MD_CODE:       return (FWStyle){ s * 3 / 4, false, 0, s / 2, s / 3, false };
    case MD_TABLE_ROW:  return (FWStyle){ s, true, 0, 0, s / 3, false };
    case MD_TABLE_CELL: return (FWStyle){ s * 7 / 8, false, f, s, s / 8, false };
    case MD_RULE:       return (FWStyle){ s, false, f, 0, s / 3, false };
    default:            return (FWStyle){ s, false, f, 0, s / 3, false };
    }
}
FWStyle fw_block_style(const MdBlock *b, int s, int f) {
    FWStyle st = base_style(b, s, f);
    if (b->link && !st.bold) { st.family = 0; st.underline = true; }   /* 고딕 + underline */
    else if (b->link) st.underline = true;
    return st;
}
/* Text as shown: the list/quote marker goes in front of the first fragment only. */
static char *display_text(const MdBlock *b, size_t offset) {
    const char *t = b->text + offset; const char *prefix = "";
    char marker[32] = "";
    if (offset == 0) {
        if (b->kind == MD_LIST_ITEM) { snprintf(marker, sizeof(marker), "%s ", b->marker); prefix = marker; }
        else if (b->kind == MD_QUOTE) prefix = "│ ";
    }
    const char *suffix = "";
    size_t pn = strlen(prefix), tn = strlen(t), sn = strlen(suffix); char *out = malloc(pn + tn + sn + 1);
    if (out) { memcpy(out, prefix, pn); memcpy(out + pn, t, tn); memcpy(out + pn + tn, suffix, sn + 1); }
    return out;
}
static size_t utf8_back(const char *s, size_t n) { while (n && ((unsigned char)s[n] & 0xc0) == 0x80) n--; return n; }
/* Largest prefix of b->text[offset..] (bytes) that fits in `available`, ending
   at a space or newline when one is near; 0 when not even one character fits. */
static int fit_prefix(const MdBlock *b, size_t offset, const FWRenderer *r, const FWStyle *st, int width, int available, size_t *out) {
    const char *t = b->text + offset; size_t n = strlen(t), lo = 0, hi = n;
    char *buf = malloc(n + 40); if (!buf) return -1;
    const char *prefix = (offset == 0 && b->kind == MD_QUOTE) ? "│ " : ""; char marker[32] = "";
    if (offset == 0 && b->kind == MD_LIST_ITEM) { snprintf(marker, sizeof(marker), "%s ", b->marker); prefix = marker; }
    size_t pn = strlen(prefix); memcpy(buf, prefix, pn);
    while (lo < hi) {   /* binary search on character boundaries */
        size_t mid = utf8_back(t, (lo + hi + 1) / 2); if (mid <= lo) { mid = lo + 1; while (mid < n && ((unsigned char)t[mid] & 0xc0) == 0x80) mid++; }
        memcpy(buf + pn, t, mid); buf[pn + mid] = 0;
        bool fits = false; int h = 0; if (r->measure(r->ctx, buf, st, width, available, &fits, &h) < 0) { free(buf); return -1; }
        if (fits) lo = mid; else { size_t prev = utf8_back(t, mid - 1); hi = prev > lo ? prev : lo; if (hi == lo) break; }
    }
    free(buf);
    if (lo && lo < n) {   /* prefer breaking after a space or newline within the last ~24 characters */
        for (size_t i = lo, steps = 0; i > 0 && steps < 72; i--, steps++)
            if (t[i - 1] == ' ' || t[i - 1] == '\n') { lo = i; break; }
    }
    *out = lo; return 0;
}
int fw_layout_page(const MdDocument *doc, FWPos pos, const FWRenderer *r, FWArea area, int base, int body_family, bool draw, FWPos *next) {
    if (pos.block >= doc->count) { *next = pos; return 1; }
    int y = area.y, bottom = area.y + area.h; bool first = true;
    while (pos.block < doc->count) {
        const MdBlock *b = &doc->blocks[pos.block]; FWStyle st = fw_block_style(b, base, body_family);
        int gap = first ? 0 : st.space_before; int x = area.x + st.indent, width = area.w - st.indent;
        if (width < base * 4) { x = area.x; width = area.w; }
        if (b->kind == MD_RULE) {
            int h = st.size; if (y + gap + h > bottom) break;
            if (draw && r->rule(r->ctx, x, y + gap + h / 2, width) < 0) return -1;
            y += gap + h; pos.block++; pos.offset = 0; first = false; continue;
        }
        int available = bottom - y - gap; if (available <= 0) break;
        char *text = display_text(b, pos.offset); if (!text) return -1;
        if (!*text) { free(text); pos.block++; pos.offset = 0; continue; }
        bool fits = false; int h = 0;
        if (r->measure(r->ctx, text, &st, width, available, &fits, &h) < 0) { free(text); return -1; }
        if (fits) {
            if (draw && r->draw(r->ctx, text, &st, x, y + gap, width, h) < 0) { free(text); return -1; }
            if (draw && r->hit && r->hit(r->ctx, pos.block, area.x, y + gap, area.w, h) < 0) { free(text); return -1; }
            free(text); y += gap + h; pos.block++; pos.offset = 0; first = false; continue;
        }
        free(text);
        size_t take = 0; if (fit_prefix(b, pos.offset, r, &st, width, available, &take) < 0) return -1;
        if (!take) {
            if (!first) break;   /* nothing fits below the previous block: next page */
            take = 1; while (b->text[pos.offset + take] && ((unsigned char)b->text[pos.offset + take] & 0xc0) == 0x80) take++;
        }
        /* draw the fitting fragment, continue the rest on the next page */
        MdBlock part = *b; char *frag = malloc(take + 1); if (!frag) return -1;
        memcpy(frag, b->text + pos.offset, take); frag[take] = 0;
        char *shown = NULL;
        { MdBlock tmp = part; tmp.text = frag; shown = display_text(&tmp, 0); if (pos.offset) { free(shown); shown = strdup(frag); } }
        free(frag); if (!shown) return -1;
        if (r->measure(r->ctx, shown, &st, width, available, &fits, &h) < 0) { free(shown); return -1; }
        if (draw && r->draw(r->ctx, shown, &st, x, y + gap, width, h) < 0) { free(shown); return -1; }
        if (draw && r->hit && r->hit(r->ctx, pos.block, area.x, y + gap, area.w, h) < 0) { free(shown); return -1; }
        free(shown);
        pos.offset += take; while (b->text[pos.offset] == ' ' || b->text[pos.offset] == '\n') pos.offset++;
        if (!b->text[pos.offset]) { pos.block++; pos.offset = 0; }
        break;
    }
    *next = pos; return 0;
}
