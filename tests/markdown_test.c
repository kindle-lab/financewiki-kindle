/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "markdown.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void inl(const char *in, const char *want) { char *got = markdown_inline(in, strlen(in)); if (strcmp(got, want)) { fprintf(stderr, "inline %s -> [%s] want [%s]\n", in, got, want); assert(0); } free(got); }
int main(void) {
    /* 2. no damage to underscores/asterisks that are not emphasis */
    inl("last_verified 20260903_고가주택 snake_case_name", "last_verified 20260903_고가주택 snake_case_name");
    inl("**PER = PBR ÷ ROE** 항등식", "PER = PBR ÷ ROE 항등식");
    inl("*기울임* _밑줄_ __굵게__ ***둘다*** ~~취소~~", "기울임 밑줄 굵게 둘다 취소");
    inl("2 * 3 * 4 = 24, a*b", "2 * 3 * 4 = 24, a*b");
    inl("`PBR 상승·PER 하락` 디커플링, `a_b*c`", "PBR 상승·PER 하락 디커플링, a_b*c");
    inl("[[위키 인덱스]] [[wiki/a.md|대상]] [[금리#정의]] [링크](https://x.y) ![차트](c.png) ![[그림.png]]", "위키 인덱스 대상 금리 링크 [그림: 차트] [그림: 그림.png]");
    inl("\\*별표\\* a\\_b 1\\.", "*별표* a_b 1.");
    inl("줄<br>바꿈 <sup>1</sup> <https://example.com>", "줄\n바꿈 1 https://example.com");
    inl("**닫히지 않음", "**닫히지 않음");
    inl("ROE<r)와 이익 고점의 낮은 ω·ROE>r)로, 1 < 2 > 0, <div class=\"x\">본문</div>", "ROE<r)와 이익 고점의 낮은 ω·ROE>r)로, 1 < 2 > 0, 본문");
    const char *md =
        "---\ntitle: 제목\ncategory: concept\n---\n\n"
        "# ROE 지속성\n\n## 정의\n\nPER, PBR은\n독립 지표가 아니다.  \n강제 줄바꿈.\n\n"
        "- `wiki/`\n  - [[위키 인덱스]]\n  - [[브리핑 인덱스]]\n- `sources/`\n1. 첫째\n2) 둘째\n- [ ] 할 일\n- [x] 끝\n\n"
        "| 항목 | 결과 | 판단 반영 |\n|---|---|---|\n| 미국 8월 고용 | +16.2만명 | 제약 약화 |\n| 빈칸 |  | 값 |\n\n"
        "> 인용 첫줄\n> 둘째줄\n\n---\n\n```\ncode_with *stars*\n  indented\n```\n";
    MdDocument d = {0}; assert(markdown_parse(md, &d) == 0);
    const struct { MdKind k; int level; const char *marker, *text; } want[] = {
        { MD_HEADING, 1, "", "ROE 지속성" }, { MD_HEADING, 2, "", "정의" },
        { MD_PARAGRAPH, 0, "", "PER, PBR은 독립 지표가 아니다.\n강제 줄바꿈." },
        { MD_LIST_ITEM, 0, "•", "wiki/" }, { MD_LIST_ITEM, 1, "◦", "위키 인덱스" }, { MD_LIST_ITEM, 1, "◦", "브리핑 인덱스" },
        { MD_LIST_ITEM, 0, "•", "sources/" }, { MD_LIST_ITEM, 0, "1.", "첫째" }, { MD_LIST_ITEM, 0, "2.", "둘째" },
        { MD_LIST_ITEM, 0, "☐", "할 일" }, { MD_LIST_ITEM, 0, "☑", "끝" },
        { MD_TABLE_ROW, 0, "", "미국 8월 고용" }, { MD_TABLE_CELL, 1, "", "결과: +16.2만명" }, { MD_TABLE_CELL, 1, "", "판단 반영: 제약 약화" },
        { MD_TABLE_ROW, 0, "", "빈칸" }, { MD_TABLE_CELL, 1, "", "판단 반영: 값" },
        { MD_QUOTE, 1, "", "인용 첫줄 둘째줄" }, { MD_RULE, 0, "", "" }, { MD_CODE, 0, "", "code_with *stars*\n  indented" },
    };
    size_t n = sizeof(want) / sizeof(want[0]);
    for (size_t i = 0; i < d.count && i < n; i++)
        if (d.blocks[i].kind != want[i].k || d.blocks[i].level != want[i].level || strcmp(d.blocks[i].marker, want[i].marker) || strcmp(d.blocks[i].text, want[i].text)) {
            fprintf(stderr, "block %zu: kind=%d level=%d marker=[%s] text=[%s]\n", i, d.blocks[i].kind, d.blocks[i].level, d.blocks[i].marker, d.blocks[i].text); assert(0); }
    assert(d.count == n);
    assert(d.blocks[4].link && !strcmp(d.blocks[4].link, "위키 인덱스"));   /* "  - [[위키 인덱스]]" */
    assert(!d.blocks[2].link && !d.blocks[3].link);                          /* prose and "`wiki/`" are not links */
    markdown_free(&d);
    assert(markdown_parse("- **[[금리|기준금리]]**\n- [기사](../articles/a.md)\n- [외부](https://x.y)\n- 본문 [[a]] 중간\n| [[회사]] | 값 |\n|---|---|\n| [[삼성]] | 1 |\n", &d) == 0);
    assert(!strcmp(d.blocks[0].link, "금리") && !strcmp(d.blocks[0].text, "기준금리"));
    assert(!strcmp(d.blocks[1].link, "../articles/a.md") && !d.blocks[2].link && !d.blocks[3].link);
    assert(d.blocks[4].kind == MD_TABLE_ROW && !strcmp(d.blocks[4].link, "삼성"));
    markdown_free(&d);
    /* front matter without a closing line is ordinary text; empty input is fine */
    assert(markdown_parse("---\n본문만", &d) == 0 && d.count >= 1); markdown_free(&d);
    assert(markdown_parse("", &d) == 0 && d.count == 0); markdown_free(&d);
    puts("markdown: front matter, emphasis pairing, intraword underscores, code, links, images, nested lists, tables, quotes, rules passed");
}
