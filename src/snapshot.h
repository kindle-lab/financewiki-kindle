/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FW_SNAPSHOT_H
#define FW_SNAPSHOT_H
#include <stdbool.h>
#include <stddef.h>
typedef struct sqlite3 sqlite3;
typedef struct { sqlite3 *db; int count; } Snapshot;
/* A document's offset is its position in path order (0..count-1). */
typedef struct { char *id, *title, *body, *category, *directory; } SnapshotDoc;
/* One row of the folder browser: a subfolder (offset -1) or a document. */
typedef struct { bool folder; int offset; char *label; char *path; } SnapshotEntry;
typedef struct { SnapshotEntry *items; int count; } SnapshotList;
int snapshot_open(Snapshot *snapshot, const char *path);
void snapshot_close(Snapshot *snapshot);
int snapshot_load(Snapshot *snapshot, int offset, SnapshotDoc *document);
void snapshot_doc_free(SnapshotDoc *document);
/* Offset of an exact repository path, or -1. */
int snapshot_offset_of(Snapshot *snapshot, const char *path);
/* Subfolders (by name) then documents (index pages first, then by title) of one
   directory, like the mobile viewer's tree. "" is the repository root. */
int snapshot_folder(Snapshot *snapshot, const char *directory, SnapshotList *out);
void snapshot_list_free(SnapshotList *list);
/* Resolves a wiki-link target the way the mobile viewer does: relative path,
   then path/slug/title/alias lookup preferring the current directory. */
int snapshot_resolve(Snapshot *snapshot, const char *target, const char *current_path);
bool snapshot_page(const char *body, unsigned page, char *out, size_t capacity);
bool snapshot_page_limited(const char *body, unsigned page, size_t page_bytes, char *out, size_t capacity);
#endif
