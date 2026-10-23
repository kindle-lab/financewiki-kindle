/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "markdown.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct { char *text; size_t used, capacity; } Buffer;
static int put(Buffer *b, char c) {
    if (b->used + 2 > b->capacity) { size_t n = b->capacity ? b->capacity * 2 : 256; char *p = realloc(b->text, n); if (!p) return -1; b->text = p; b->capacity = n; }
    b->text[b->used++] = c; b->text[b->used] = 0; return 0;
}
static int put_n(Buffer *b, const char *s, size_t n) { for (size_t i = 0; i < n; i++) if (put(b, s[i])) return -1; return 0; }
static int put_s(Buffer *b, const char *s) { return put_n(b, s, strlen(s)); }
static char *take(Buffer *b) { if (!b->text) { b->text = calloc(1, 1); } char *t = b->text; *b = (Buffer){0}; return t; }

/* ---- inline ---------------------------------------------------------- */
static bool is_space(int c) { return c == ' ' || c == '\t' || c == '\n' || c == 0; }
static bool is_word(unsigned char c) { return isalnum(c) || c >= 0x80; }
static bool is_punct_escape(char c) { return c && strchr("\\`*_{}[]()#+-.!|~<>\"'$%&,/:;=?@^", c) != NULL; }
/* Flanking rules (CommonMark, simplified): '_' never opens/closes inside a word. */
static bool can_open(const char *s, size_t i, size_t n, size_t run, char c) {
    char next = i + run < n ? s[i + run] : 0; char prev = i ? s[i - 1] : 0;
    if (is_space(next)) return false;
    if (c == '_' && prev && is_word((unsigned char)prev)) return false;
    return true;
}
static bool can_close(const char *s, size_t i, size_t n, size_t run, char c) {
    char prev = i ? s[i - 1] : 0; char next = i + run < n ? s[i + run] : 0;
    if (!prev || is_space(prev)) return false;
    if (c == '_' && next && is_word((unsigned char)next)) return false;
    return true;
}
/* Index of a closing delimiter run of exactly `run` chars `c`, or (size_t)-1. */
static size_t find_close(const char *s, size_t from, size_t n, size_t run, char c) {
    for (size_t i = from; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n) { i++; continue; }
        if (s[i] == '`') { const char *e = memchr(s + i + 1, '`', n - i - 1); if (e) { i = (size_t)(e - s); continue; } }
        if (s[i] != c) continue;
        size_t r = 0; while (i + r < n && s[i + r] == c) r++;
        if (r == run && can_close(s, i, n, run, c)) return i;
        i += r - 1;
    }
    return (size_t)-1;
}
static int inline_into(Buffer *b, const char *s, size_t n);
/* Only real HTML tags count: "<r)와 ... ROE>" in "ROE<r" is text, not a tag. */
static bool html_tag(const char *t, size_t len, size_t *name_len) {
    static const char *const known[] = { "a","abbr","b","br","center","code","del","details","div","em","font","hr","i","img","ins","kbd","mark","p","q","s","small","span","strong","sub","summary","sup","table","tbody","td","th","thead","tr","u", NULL };
    size_t i = 1; if (i < len && t[i] == '/') i++;
    size_t start = i; while (i < len && isalpha((unsigned char)t[i])) i++;
    size_t n = i - start; if (!n || n > 8) return false;
    if (t[i] != '>' && t[i] != ' ' && t[i] != '/') return false;
    for (const char *const *k = known; *k; k++) if (strlen(*k) == n && !strncasecmp(t + start, *k, n)) { *name_len = n; return true; }
    return false;
}
static int image_label(Buffer *b, const char *alt, size_t n) {
    if (put_s(b, "[그림")) return -1;
    if (n) { if (put_s(b, ": ") || inline_into(b, alt, n)) return -1; }
    return put(b, ']');
}
static int inline_into(Buffer *b, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\\' && i + 1 < n && is_punct_escape(s[i + 1])) { if (put(b, s[++i])) return -1; continue; }
        if (c == '`') {   /* code span: content verbatim */
            size_t run = 0; while (i + run < n && s[i + run] == '`') run++;
            for (size_t j = i + run; j + run <= n; j++) {
                size_t r = 0; while (j + r < n && s[j + r] == '`') r++;
                if (r == run) { size_t a = i + run, z = j; while (a < z && s[a] == ' ') a++; while (z > a && s[z - 1] == ' ') z--; if (put_n(b, s + a, z - a)) return -1; i = j + run - 1; goto next; }
                if (r) j += r - 1;
            }
            if (put_n(b, s + i, run)) return -1; i += run - 1; continue;
        }
        if ((c == '!' && i + 2 < n && s[i + 1] == '[' && s[i + 2] == '[') || (c == '[' && i + 1 < n && s[i + 1] == '[')) {
            bool image = c == '!'; size_t start = i + (image ? 3 : 2); const char *close = NULL;
            for (size_t j = start; j + 1 < n; j++) if (s[j] == ']' && s[j + 1] == ']') { close = s + j; break; }
            if (close) {
                const char *bar = memchr(s + start, '|', (size_t)(close - (s + start)));
                const char *lab = bar ? bar + 1 : s + start; size_t ln = (size_t)(close - lab);
                if (image) { const char *t = s + start; size_t tn = (size_t)((bar ? bar : close) - t); if (image_label(b, bar ? lab : t, bar ? ln : tn)) return -1; }
                else { if (!bar) { const char *hash = memchr(lab, '#', ln); if (hash && hash != lab) ln = (size_t)(hash - lab); } if (inline_into(b, lab, ln)) return -1; }
                i = (size_t)(close - s) + 1; continue;
            }
        }
        if (c == '[' || (c == '!' && i + 1 < n && s[i + 1] == '[')) {
            bool image = c == '!'; size_t open = i + (image ? 1 : 0); int depth = 0; size_t close = (size_t)-1;
            for (size_t j = open; j < n; j++) { if (s[j] == '\\') { j++; continue; } if (s[j] == '[') depth++; else if (s[j] == ']' && --depth == 0) { close = j; break; } }
            if (close != (size_t)-1 && close + 1 < n && s[close + 1] == '(') {
                size_t end = (size_t)-1; int pd = 0;
                for (size_t j = close + 1; j < n; j++) { if (s[j] == '(') pd++; else if (s[j] == ')' && --pd == 0) { end = j; break; } }
                if (end != (size_t)-1) {
                    if (image) { if (image_label(b, s + open + 1, close - open - 1)) return -1; }
                    else if (inline_into(b, s + open + 1, close - open - 1)) return -1;
                    i = end; continue;
                }
            }
        }
        if (c == '<') {   /* autolink or simple HTML tag */
            const char *gt = memchr(s + i, '>', n - i);
            if (gt) {
                size_t len = (size_t)(gt - (s + i)) + 1;
                if (len > 2 && (!strncmp(s + i + 1, "http", 4) || !strncmp(s + i + 1, "mailto:", 7))) { if (put_n(b, s + i + 1, len - 2)) return -1; i += len - 1; continue; }
                size_t name = 0; if (html_tag(s + i, len, &name)) {
                    if (name == 2 && !strncasecmp(s + i + 1 + (s[i + 1] == '/'), "br", 2)) { if (put(b, '\n')) return -1; }
                    i += len - 1; continue;
                }
            }
        }
        if (c == '*' || c == '_' || c == '~') {
            size_t run = 0; while (i + run < n && s[i + run] == c) run++;
            if (c == '~' && run != 2) { if (put_n(b, s + i, run)) return -1; i += run - 1; continue; }
            if (run <= 3 && can_open(s, i, n, run, c)) {
                size_t close = find_close(s, i + run, n, run, c);
                if (close != (size_t)-1) { if (inline_into(b, s + i + run, close - i - run)) return -1; i = close + run - 1; continue; }
            }
            if (put_n(b, s + i, run)) return -1; i += run - 1; continue;
        }
        if (put(b, c)) return -1;
    next:;
    }
    return 0;
}
char *markdown_inline(const char *text, size_t length) { Buffer b = {0}; if (inline_into(&b, text, length)) { free(b.text); return NULL; } return take(&b); }

/* ---- blocks ----------------------------------------------------------- */
typedef struct { const char *s; size_t n; } Line;
static int add(MdDocument *d, MdKind kind, int level, const char *marker, char *text) {
    if (!text) return -1;
    MdBlock *grown = realloc(d->blocks, (d->count + 1) * sizeof(*grown)); if (!grown) { free(text); return -1; }
    d->blocks = grown; char *m = strdup(marker ? marker : ""); if (!m) { free(text); return -1; }
    d->blocks[d->count++] = (MdBlock){ kind, level, m, text, NULL }; return 0;
}
static bool external_target(const char *t, size_t n) {
    return (n >= 5 && !strncasecmp(t, "http:", 5)) || (n >= 6 && !strncasecmp(t, "https:", 6)) || (n >= 7 && !strncasecmp(t, "mailto:", 7));
}
/* Target of a block whose raw text is exactly one internal link, optionally
   wrapped in emphasis: "[[a]]", "[[a|b]]", "**[[a]]**", "[b](a.md)". */
static char *link_only(const char *s, size_t n) {
    while (n && (*s == ' ' || *s == '*' || *s == '_')) { s++; n--; }
    while (n && (s[n - 1] == ' ' || s[n - 1] == '*' || s[n - 1] == '_' || s[n - 1] == '\r')) n--;
    if (n > 4 && s[0] == '[' && s[1] == '[' && s[n - 2] == ']' && s[n - 1] == ']') {
        const char *in = s + 2; size_t m = n - 4;
        for (size_t i = 0; i + 1 < m; i++) if (in[i] == ']' && in[i + 1] == ']') return NULL;
        const char *bar = memchr(in, '|', m); size_t tn = bar ? (size_t)(bar - in) : m;
        while (tn && in[tn - 1] == ' ') tn--; while (tn && *in == ' ') { in++; tn--; }
        return tn ? strndup(in, tn) : NULL;
    }
    if (n > 4 && s[0] == '[' && s[n - 1] == ')') {
        const char *mid = NULL; for (size_t i = 1; i + 1 < n; i++) if (s[i] == ']' && s[i + 1] == '(') { mid = s + i; break; }
        if (!mid) return NULL; const char *t = mid + 2; size_t tn = (size_t)(s + n - 1 - t);
        while (tn && *t == ' ') { t++; tn--; } const char *sp = memchr(t, ' ', tn); if (sp) tn = (size_t)(sp - t);
        if (!tn || external_target(t, tn) || *t == '#') return NULL;
        return strndup(t, tn);
    }
    return NULL;
}
static int set_link(MdDocument *d, const char *raw, size_t n) {
    if (!d->count) return 0; MdBlock *b = &d->blocks[d->count - 1];
    if (b->kind == MD_CODE || b->kind == MD_RULE) return 0;
    b->link = link_only(raw, n); return 0;
}
static size_t indent_of(Line l, size_t *pos) { size_t col = 0, i = 0; while (i < l.n && (l.s[i] == ' ' || l.s[i] == '\t')) { col += l.s[i] == '\t' ? 4 : 1; i++; } *pos = i; return col; }
static bool blank(Line l) { for (size_t i = 0; i < l.n; i++) if (!isspace((unsigned char)l.s[i])) return false; return true; }
static bool is_rule(Line l) {
    size_t i; indent_of(l, &i); if (i >= l.n) return false; char c = l.s[i]; if (c != '-' && c != '*' && c != '_') return false;
    int count = 0; for (; i < l.n; i++) { if (l.s[i] == c) count++; else if (l.s[i] != ' ' && l.s[i] != '\t' && l.s[i] != '\r') return false; }
    return count >= 3;
}
static bool fence(Line l, char *ch, size_t *len) {
    size_t i; indent_of(l, &i); if (i + 3 > l.n) return false; char c = l.s[i]; if (c != '`' && c != '~') return false;
    size_t r = 0; while (i + r < l.n && l.s[i + r] == c) r++; if (r < 3) return false; *ch = c; *len = r; return true;
}
static int heading_level(Line l, size_t *text) {
    size_t i; indent_of(l, &i); int level = 0; while (i < l.n && l.s[i] == '#' && level < 7) { level++; i++; }
    if (level < 1 || level > 6 || (i < l.n && l.s[i] != ' ' && l.s[i] != '\t' && l.s[i] != '\r')) return 0;
    while (i < l.n && (l.s[i] == ' ' || l.s[i] == '\t')) i++; *text = i; return level;
}
/* List marker: "-", "*", "+" or "12." / "12)" followed by a space. */
static bool list_marker(Line l, size_t *indent, size_t *text, char *marker, size_t cap) {
    size_t i; *indent = indent_of(l, &i); if (i >= l.n) return false;
    if ((l.s[i] == '-' || l.s[i] == '*' || l.s[i] == '+') && i + 1 < l.n && (l.s[i + 1] == ' ' || l.s[i + 1] == '\t')) { marker[0] = 0; *text = i + 2; }
    else { size_t j = i; while (j < l.n && isdigit((unsigned char)l.s[j]) && j - i < 9) j++;
        if (j == i || j + 1 >= l.n || (l.s[j] != '.' && l.s[j] != ')') || (l.s[j + 1] != ' ' && l.s[j + 1] != '\t')) return false;
        size_t m = j - i + 1 < cap - 1 ? j - i + 1 : cap - 1; memcpy(marker, l.s + i, m); marker[m] = 0; if (marker[m - 1] == ')') marker[m - 1] = '.'; *text = j + 2; }
    while (*text < l.n && l.s[*text] == ' ') (*text)++;
    return true;
}
static bool table_line(Line l) { size_t i; indent_of(l, &i); return i < l.n && l.s[i] == '|'; }
static bool table_separator(Line l) {
    if (!table_line(l)) return false; bool dash = false;
    for (size_t i = 0; i < l.n; i++) { char c = l.s[i]; if (c == '-') dash = true; else if (c != '|' && c != ':' && c != ' ' && c != '\t' && c != '\r') return false; }
    return dash;
}
/* Splits "| a | b |" into trimmed cells (escaped "\|" stays in the cell). */
static int cells(Line l, Line *out, int max) {
    size_t i; indent_of(l, &i); size_t end = l.n; while (end > i && (l.s[end - 1] == ' ' || l.s[end - 1] == '\r' || l.s[end - 1] == '\t')) end--;
    if (i < end && l.s[i] == '|') i++; if (end > i && l.s[end - 1] == '|' && (end < 2 || l.s[end - 2] != '\\')) end--;
    int n = 0; size_t start = i;
    for (size_t j = i; j <= end && n < max; j++) {
        if (j == end || (l.s[j] == '|' && (j == 0 || l.s[j - 1] != '\\'))) {
            size_t a = start, z = j; while (a < z && (l.s[a] == ' ' || l.s[a] == '\t')) a++; while (z > a && (l.s[z - 1] == ' ' || l.s[z - 1] == '\t')) z--;
            out[n++] = (Line){ l.s + a, z - a }; start = j + 1;
        }
    }
    return n;
}
static char *joined_inline(const Buffer *raw) { return markdown_inline(raw->text ? raw->text : "", raw->used); }
/* Appends one source line to a paragraph buffer. A hard break (two trailing
   spaces or a backslash) keeps the newline; otherwise lines join with a space. */
static int append_line(Buffer *raw, const char *s, size_t n) {
    while (n && (s[n - 1] == '\r')) n--;
    bool hard = (n >= 2 && s[n - 1] == ' ' && s[n - 2] == ' ') || (n && s[n - 1] == '\\');
    if (n && s[n - 1] == '\\') n--;
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    size_t a = 0; while (a < n && (s[a] == ' ' || s[a] == '\t')) a++;
    if (raw->used && raw->text[raw->used - 1] != '\n' && put(raw, ' ')) return -1;
    if (put_n(raw, s + a, n - a)) return -1;
    return hard ? put(raw, '\n') : 0;
}
static const char *bullet(int level) { static const char *const b[] = { "•", "◦", "▪" }; return b[level % 3]; }
static Line next_line(const char **p) {
    const char *s = *p, *e = strchr(s, '\n'); size_t n = e ? (size_t)(e - s) : strlen(s);
    *p = e ? e + 1 : s + n; if (n && s[n - 1] == '\r') n--; return (Line){ s, n };
}
int markdown_parse(const char *source, MdDocument *out) {
    *out = (MdDocument){0}; const char *p = source ? source : "";
    /* YAML front matter: "---" on the first line up to the next "---" or "...". */
    if (!strncmp(p, "---", 3) && (p[3] == '\n' || p[3] == '\r')) {
        const char *q = p; next_line(&q); const char *scan = q;
        while (*scan) { const char *at = scan; Line l = next_line(&scan);
            if ((l.n == 3 && (!strncmp(l.s, "---", 3) || !strncmp(l.s, "...", 3)))) { p = scan; break; }
            (void)at; }
    }
    Buffer raw = {0}; MdKind kind = MD_PARAGRAPH; int level = 0; char marker[16] = "";
    size_t list_text_col = 0; bool open = false;
#define FLUSH() do { if (open) { if (add(out, kind, level, marker, joined_inline(&raw))) goto fail; set_link(out, raw.text ? raw.text : "", raw.used); free(raw.text); raw = (Buffer){0}; open = false; } } while (0)
    while (*p) {
        Line l = next_line(&p); size_t pos; size_t col = indent_of(l, &pos);
        char fc; size_t flen;
        if (fence(l, &fc, &flen)) {
            FLUSH(); Buffer code = {0};
            while (*p) { Line c = next_line(&p); char c2; size_t l2; if (fence(c, &c2, &l2) && c2 == fc && l2 >= flen) break;
                size_t strip = 0; while (strip < c.n && strip < col && c.s[strip] == ' ') strip++;
                if (code.used && put(&code, '\n')) { free(code.text); goto fail; }
                if (put_n(&code, c.s + strip, c.n - strip)) { free(code.text); goto fail; } }
            if (add(out, MD_CODE, 0, "", take(&code))) goto fail; continue;
        }
        if (blank(l)) { FLUSH(); continue; }
        size_t htext; int h = heading_level(l, &htext);
        if (h) { FLUSH(); size_t n = l.n; while (n > htext && (l.s[n - 1] == '#' || l.s[n - 1] == ' ')) n--; if (n < htext) n = htext;
            if (add(out, MD_HEADING, h, "", markdown_inline(l.s + htext, n - htext))) goto fail; set_link(out, l.s + htext, n - htext); continue; }
        if (is_rule(l)) { FLUSH(); if (add(out, MD_RULE, 0, "", strdup(""))) goto fail; continue; }
        if (table_line(l)) {
            const char *peek = p; Line sep = *p ? next_line(&peek) : (Line){ "", 0 };
            if (table_separator(sep)) {
                FLUSH(); Line head[32]; int hn = cells(l, head, 32); p = peek;
                while (*p) { const char *save = p; Line r = next_line(&p); if (!table_line(r)) { p = save; break; }
                    Line c[32]; int cn = cells(r, c, 32); if (!cn) continue;
                    if (add(out, MD_TABLE_ROW, 0, "", markdown_inline(c[0].s, c[0].n))) goto fail; set_link(out, c[0].s, c[0].n);
                    for (int k = 1; k < cn; k++) { if (!c[k].n) continue; Buffer cell = {0};
                        if (k < hn && head[k].n) { char *hd = markdown_inline(head[k].s, head[k].n); if (!hd || put_s(&cell, hd) || put_s(&cell, ": ")) { free(hd); free(cell.text); goto fail; } free(hd); }
                        char *v = markdown_inline(c[k].s, c[k].n); if (!v || put_s(&cell, v)) { free(v); free(cell.text); goto fail; } free(v);
                        if (add(out, MD_TABLE_CELL, 1, "", take(&cell))) goto fail; }
                }
                continue;
            }
        }
        if (l.s[pos] == '>') {
            int depth = 0; size_t i = pos; while (i < l.n && (l.s[i] == '>' || l.s[i] == ' ')) { if (l.s[i] == '>') depth++; i++; }
            if (!(open && kind == MD_QUOTE && level == depth)) { FLUSH(); kind = MD_QUOTE; level = depth; marker[0] = 0; open = true; }
            if (append_line(&raw, l.s + i, l.n - i)) goto fail; continue;
        }
        size_t lindent, ltext; char m[16];
        if (list_marker(l, &lindent, &ltext, m, sizeof(m))) {
            FLUSH(); kind = MD_LIST_ITEM; level = (int)(lindent / 2); if (level > 8) level = 8;
            const char *t = l.s + ltext; size_t tn = l.n - ltext;
            if (tn >= 3 && t[0] == '[' && (t[1] == ' ' || t[1] == 'x' || t[1] == 'X') && t[2] == ']') { snprintf(marker, sizeof(marker), "%s", t[1] == ' ' ? "☐" : "☑"); t += 3; tn -= 3; }
            else snprintf(marker, sizeof(marker), "%s", m[0] ? m : bullet(level));
            list_text_col = ltext; open = true; if (append_line(&raw, t, tn)) goto fail; continue;
        }
        /* Lazy continuation of a paragraph, quote or list item. */
        if (open && (kind != MD_LIST_ITEM || col >= 2 || list_text_col)) { if (append_line(&raw, l.s, l.n)) goto fail; continue; }
        FLUSH(); kind = MD_PARAGRAPH; level = 0; marker[0] = 0; open = true; if (append_line(&raw, l.s, l.n)) goto fail;
    }
    FLUSH();
#undef FLUSH
    return 0;
fail:
    free(raw.text); markdown_free(out); return -1;
}
void markdown_free(MdDocument *d) { for (size_t i = 0; i < d->count; i++) { free(d->blocks[i].marker); free(d->blocks[i].text); free(d->blocks[i].link); } free(d->blocks); *d = (MdDocument){0}; }
