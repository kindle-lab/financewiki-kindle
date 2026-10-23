/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FW_MARKDOWN_H
#define FW_MARKDOWN_H
#include <stddef.h>
/* A Markdown document as reading blocks. Inline markup is already resolved to
   plain text (emphasis markers, link targets and escapes removed); the reader
   chooses size, weight and indentation per kind. */
typedef enum {
    MD_PARAGRAPH,
    MD_HEADING,     /* level 1..6 */
    MD_LIST_ITEM,   /* level = nesting depth from 0; marker = "•", "◦", "1." ... */
    MD_QUOTE,       /* level = quote depth from 1 */
    MD_CODE,        /* text kept verbatim, lines separated by '\n' */
    MD_TABLE_ROW,   /* first cell of a table row (level 0) */
    MD_TABLE_CELL,  /* "header: value" for the other cells of that row */
    MD_RULE         /* horizontal rule; text is "" */
} MdKind;
/* link: target when the whole block is a single internal link (an index row such
   as "- [[금리]]"), so tapping the row can open it; NULL otherwise. */
typedef struct { MdKind kind; int level; char *marker; char *text; char *link; } MdBlock;
typedef struct { MdBlock *blocks; size_t count; } MdDocument;
/* Returns 0 on success. A leading YAML front matter block is skipped. */
int markdown_parse(const char *source, MdDocument *out);
void markdown_free(MdDocument *document);
/* Inline markup to plain text (exposed for tests). Caller frees. */
char *markdown_inline(const char *text, size_t length);
#endif
