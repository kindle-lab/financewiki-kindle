/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "index.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *fixture = "{\"version\":1,\"documents\":[{\"path\":\"wiki/one.md\",\"title\":\"\\uD55C\\uAE00\",\"hash\":\"one\",\"category\":\"wiki\",\"directory\":\"wiki\",\"aliases\":[\"ignored\"]},{\"path\":\"articles/a.md\",\"title\":\"A\",\"hash\":\"two\",\"category\":\"articles\",\"directory\":\"articles\",\"slug\":\"alpha\"}]}";
    FWIndex index = {0}; char error[128] = {0};
    assert(fw_index_parse(fixture, strlen(fixture), &index, error, sizeof(error)) == 0);
    assert(index.count == 2 && !strcmp(index.documents[0].title, "한글") && !strcmp(index.documents[1].slug, "alpha"));
    fw_index_free(&index);
    assert(fw_index_parse("{\"version\":2,\"documents\":[]}", 28, &index, error, sizeof(error)) != 0);
    const char *pretty = "{\n  \"version\": 1,\n  \"documents\": [\n    {\n      \"path\": \"index.md\",\n      \"title\": \"FinanceWiki\",\n      \"size\": 942,\n      \"hash\": \"h\",\n      \"category\": \"root\",\n      \"directory\": \"\"\n    }\n  ]\n}\n";
    assert(fw_index_parse(pretty, strlen(pretty), &index, error, sizeof(error)) == 0);
    assert(index.count == 1 && !strcmp(index.documents[0].path, "index.md") && !strcmp(index.documents[0].directory, ""));
    fw_index_free(&index);
    puts("index: FinanceWiki viewer-index v1 parsing passed");
}
