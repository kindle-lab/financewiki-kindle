/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "index.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *p, *end; char *error; size_t error_size; } Parser;

static void fail(Parser *p, const char *format, ...) {
    if (!p->error || !p->error_size || p->error[0]) return;
    va_list args; va_start(args, format); vsnprintf(p->error, p->error_size, format, args); va_end(args);
}
static void whitespace(Parser *p) { while (p->p < p->end && isspace((unsigned char)*p->p)) p->p++; }
static int take(Parser *p, char c) { whitespace(p); if (p->p >= p->end || *p->p != c) { fail(p, "expected '%c' in viewer-index.json", c); return -1; } p->p++; return 0; }
static int hex_value(char c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; }
static int append_byte(char **text, size_t *used, size_t *capacity, unsigned char value) {
    if (*used + 2 > *capacity) { size_t next = *capacity ? *capacity * 2 : 64; char *grown = realloc(*text, next); if (!grown) return -1; *text = grown; *capacity = next; }
    (*text)[(*used)++] = (char)value; (*text)[*used] = 0; return 0;
}
static int append_utf8(char **text, size_t *used, size_t *capacity, uint32_t code) {
    if (code <= 0x7f) return append_byte(text, used, capacity, (unsigned char)code);
    if (code <= 0x7ff) return append_byte(text, used, capacity, (unsigned char)(0xc0 | (code >> 6))) || append_byte(text, used, capacity, (unsigned char)(0x80 | (code & 0x3f)));
    if (code <= 0xffff) return append_byte(text, used, capacity, (unsigned char)(0xe0 | (code >> 12))) || append_byte(text, used, capacity, (unsigned char)(0x80 | ((code >> 6) & 0x3f))) || append_byte(text, used, capacity, (unsigned char)(0x80 | (code & 0x3f)));
    if (code <= 0x10ffff) return append_byte(text, used, capacity, (unsigned char)(0xf0 | (code >> 18))) || append_byte(text, used, capacity, (unsigned char)(0x80 | ((code >> 12) & 0x3f))) || append_byte(text, used, capacity, (unsigned char)(0x80 | ((code >> 6) & 0x3f))) || append_byte(text, used, capacity, (unsigned char)(0x80 | (code & 0x3f)));
    return -1;
}
static int unicode(Parser *p, uint32_t *out) {
    if (p->end - p->p < 4) return -1;
    uint32_t value = 0; for (int i = 0; i < 4; ++i) { int digit = hex_value(p->p[i]); if (digit < 0) return -1; value = (value << 4) | (uint32_t)digit; } p->p += 4; *out = value; return 0;
}
static char *string(Parser *p) {
    whitespace(p); if (p->p >= p->end || *p->p++ != '"') { fail(p, "expected JSON string"); return NULL; }
    char *result = NULL; size_t used = 0, capacity = 0;
    while (p->p < p->end) {
        unsigned char c = (unsigned char)*p->p++;
        if (c == '"') return result ? result : strdup("");
        if (c < 0x20) break;
        if (c != '\\') { if (append_byte(&result, &used, &capacity, c)) break; continue; }
        if (p->p >= p->end) break;
        c = (unsigned char)*p->p++;
        if (c == '"' || c == '\\' || c == '/') { if (append_byte(&result, &used, &capacity, c)) break; }
        else if (c == 'b') { if (append_byte(&result, &used, &capacity, '\b')) break; }
        else if (c == 'f') { if (append_byte(&result, &used, &capacity, '\f')) break; }
        else if (c == 'n') { if (append_byte(&result, &used, &capacity, '\n')) break; }
        else if (c == 'r') { if (append_byte(&result, &used, &capacity, '\r')) break; }
        else if (c == 't') { if (append_byte(&result, &used, &capacity, '\t')) break; }
        else if (c == 'u') {
            uint32_t value = 0; if (unicode(p, &value)) break;
            if (value >= 0xd800 && value <= 0xdbff && p->end - p->p >= 6 && p->p[0] == '\\' && p->p[1] == 'u') { p->p += 2; uint32_t low = 0; if (unicode(p, &low) || low < 0xdc00 || low > 0xdfff) break; value = 0x10000 + ((value - 0xd800) << 10) + (low - 0xdc00); }
            if (value >= 0xd800 && value <= 0xdfff) break;
            if (append_utf8(&result, &used, &capacity, value)) break;
        } else break;
    }
    free(result); fail(p, "invalid JSON string"); return NULL;
}
static int skip_value(Parser *p);
static int skip_array(Parser *p) { if (take(p, '[')) return -1; whitespace(p); if (p->p < p->end && *p->p == ']') { p->p++; return 0; } for (;;) { if (skip_value(p)) return -1; whitespace(p); if (p->p < p->end && *p->p == ']') { p->p++; return 0; } if (take(p, ',')) return -1; } }
static int skip_object(Parser *p) { if (take(p, '{')) return -1; whitespace(p); if (p->p < p->end && *p->p == '}') { p->p++; return 0; } for (;;) { char *key = string(p); if (!key || take(p, ':') || skip_value(p)) { free(key); return -1; } free(key); whitespace(p); if (p->p < p->end && *p->p == '}') { p->p++; return 0; } if (take(p, ',')) return -1; } }
static int skip_value(Parser *p) { whitespace(p); if (p->p >= p->end) return -1; if (*p->p == '"') { char *value = string(p); if (!value) return -1; free(value); return 0; } if (*p->p == '{') return skip_object(p); if (*p->p == '[') return skip_array(p); const char *start = p->p; while (p->p < p->end && !strchr(",]} \t\r\n", *p->p)) p->p++; if (p->p == start) return -1; return 0; }
static void document_free(FWIndexDocument *d) { free(d->path); free(d->title); free(d->hash); free(d->category); free(d->directory); free(d->slug); free(d->modified); free(d->created); free(d->aliases); *d = (FWIndexDocument){0}; }
/* Reads ["a","b"] as "a\nb". Non-string items are skipped. */
static char *string_array(Parser *p) {
    if (take(p, '[')) return NULL; size_t used = 0; char *joined = calloc(1, 1); if (!joined) return NULL;
    whitespace(p); if (p->p < p->end && *p->p == ']') { p->p++; return joined; }
    for (;;) {
        whitespace(p);
        if (p->p < p->end && *p->p == '"') {
            char *item = string(p); if (!item) { free(joined); return NULL; }
            size_t n = strlen(item); char *grown = realloc(joined, used + n + 2); if (!grown) { free(item); free(joined); return NULL; }
            joined = grown; if (used) joined[used++] = '\n'; memcpy(joined + used, item, n); used += n; joined[used] = 0; free(item);
        } else if (skip_value(p)) { free(joined); return NULL; }
        whitespace(p); if (p->p < p->end && *p->p == ']') { p->p++; return joined; }
        if (take(p, ',')) { free(joined); return NULL; }
    }
}
static char **field(FWIndexDocument *d, const char *key) {
    if (!strcmp(key, "path")) return &d->path; if (!strcmp(key, "title")) return &d->title; if (!strcmp(key, "hash")) return &d->hash;
    if (!strcmp(key, "category")) return &d->category; if (!strcmp(key, "directory")) return &d->directory; if (!strcmp(key, "slug")) return &d->slug;
    if (!strcmp(key, "modified")) return &d->modified; if (!strcmp(key, "created")) return &d->created; return NULL;
}
static int document(Parser *p, FWIndexDocument *out) {
    if (take(p, '{')) return -1; whitespace(p); if (p->p < p->end && *p->p == '}') { p->p++; return -1; }
    for (;;) {
        char *key = string(p); if (!key || take(p, ':')) { free(key); goto fail; }
        whitespace(p); /* allow pretty-printed JSON: "key": "value" */
        if (!strcmp(key, "aliases") && p->p < p->end && *p->p == '[') {
            char *aliases = string_array(p); if (!aliases) { free(key); goto fail; } free(out->aliases); out->aliases = aliases;
            free(key); whitespace(p); if (p->p < p->end && *p->p == '}') { p->p++; break; } if (take(p, ',')) goto fail; continue;
        }
        char **slot = field(out, key);
        if (slot && (p->p < p->end && *p->p == '"')) { char *value = string(p); if (!value) { free(key); goto fail; } free(*slot); *slot = value; }
        else if (skip_value(p)) { free(key); goto fail; }
        free(key); whitespace(p); if (p->p < p->end && *p->p == '}') { p->p++; break; } if (take(p, ',')) goto fail;
    }
    if (!out->path || !out->title || !out->hash || !out->category || !out->directory) { fail(p, "viewer-index document is missing required fields"); goto fail; }
    return 0;
fail: document_free(out); return -1;
}
static int documents(Parser *p, FWIndex *out) {
    if (take(p, '[')) return -1; whitespace(p); if (p->p < p->end && *p->p == ']') { p->p++; return 0; }
    for (;;) {
        FWIndexDocument d = {0}; if (document(p, &d)) return -1;
        FWIndexDocument *grown = realloc(out->documents, (out->count + 1) * sizeof(*grown)); if (!grown) { document_free(&d); fail(p, "out of memory"); return -1; } out->documents = grown; out->documents[out->count++] = d;
        whitespace(p); if (p->p < p->end && *p->p == ']') { p->p++; return 0; } if (take(p, ',')) return -1;
    }
}
int fw_index_parse(const char *json, size_t size, FWIndex *out, char *error, size_t error_size) {
    if (!out || !json || !size) return -1; *out = (FWIndex){0}; if (error && error_size) error[0] = 0;
    Parser p = {json, json + size, error, error_size}; int version = 0, have_documents = 0;
    if (take(&p, '{')) goto fail; whitespace(&p); while (p.p < p.end && *p.p != '}') {
        char *key = string(&p); if (!key || take(&p, ':')) { free(key); goto fail; }
        if (!strcmp(key, "version")) { whitespace(&p); if (p.p >= p.end || *p.p != '1') { free(key); fail(&p, "unsupported viewer-index version"); goto fail; } p.p++; version = 1; }
        else if (!strcmp(key, "documents")) { if (documents(&p, out)) { free(key); goto fail; } have_documents = 1; }
        else if (skip_value(&p)) { free(key); goto fail; }
        free(key); whitespace(&p); if (p.p < p.end && *p.p == '}') break; if (take(&p, ',')) goto fail; whitespace(&p);
    }
    if (take(&p, '}') || !version || !have_documents || !out->count) { if (!p.error || !p.error[0]) fail(&p, "viewer-index has no documents"); goto fail; }
    whitespace(&p); if (p.p != p.end) { fail(&p, "unexpected text after viewer-index"); goto fail; } return 0;
fail: fw_index_free(out); return -1;
}
void fw_index_free(FWIndex *index) { if (!index) return; for (size_t i = 0; i < index->count; ++i) document_free(&index->documents[i]); free(index->documents); *index = (FWIndex){0}; }
