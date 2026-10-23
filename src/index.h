/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FW_INDEX_H
#define FW_INDEX_H

#include <stddef.h>

typedef struct {
    char *path;
    char *title;
    char *hash;
    char *category;
    char *directory;
    char *slug;
    char *modified;
    char *created;
    char *aliases; /* newline-joined; NULL when absent */
} FWIndexDocument;

typedef struct {
    FWIndexDocument *documents;
    size_t count;
} FWIndex;

/* Parses FinanceWiki's root viewer-index.json (contract version 1). */
int fw_index_parse(const char *json, size_t size, FWIndex *out, char *error, size_t error_size);
void fw_index_free(FWIndex *index);

#endif
