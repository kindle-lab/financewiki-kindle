/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "core.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
bool fw_supported_display(const char *name, bool mtk, unsigned rotation,
                          unsigned width, unsigned height, unsigned bpp) {
    /* KT6 device log: native rotation 3, but the buffer is already portrait.
       This value is NOT a request to rotate pixels or touch coordinates.
       Keep this prototype limited to the observed device/mode. */
    return name && !strcmp(name,"Basic 5") && mtk && rotation==3 &&
           width==1072 && height==1448 && bpp==8;
}
const char *fw_paragraph =
    "한글 표시와 줄바꿈을 확인합니다. 금융·경제 문서를 작은 화면에서 읽기 위한 시험입니다. "
    "긴 문장이 오른쪽 끝을 넘어가지 않는지, 받침과 문장부호가 또렷한지 확인해 주세요. "
    "FinanceWiki는 한국어와 English를 함께 표시합니다.";
const char *fw_symbols = "가나다라마바사 아자차카타파하 · ㄱ ㄴ ㅏ ㅣ\n"
    "1234567890 · 500억원 · 2조원 · 3.25% · +0.5%p\n"
    "(연결/별도) [검증] 〈제목〉 — ‘인용’ “문장”";
/* Header (title, page, folder) | content | one row of 8 buttons:
   처음 · 뒤로 · 이전 · 다음 · A- · A+ · 체 · 종료 */
Layout fw_layout(int w, int h) {
    int m = w / 24, gap = w / 96, bh = h / 16;
    int bw = (w - 2*m - 7*gap) / 8, top = h - m/2 - bh;
    int header = h * 21 / 200;                    /* 152 px on KT6 */
    Rect b[8]; for (int i = 0; i < 8; i++) b[i] = (Rect){ m + i*(bw + gap), top, bw, bh };
    Layout l = { w, h, { m, header, w - 2*m, top - m*2/3 - header },
                 b[0], b[2], b[3], b[4], b[5], b[6], b[1], b[7] };
    return l;
}
bool fw_contains(Rect r, int x, int y) {
    return x >= r.x && y >= r.y && x-r.x < r.w && y-r.y < r.h;
}
Action fw_action(Layout l, int x, int y) {
    if (fw_contains(l.exit,x,y)) return EXIT_APP;
    if (fw_contains(l.home,x,y)) return HOME;
    if (fw_contains(l.previous,x,y)) return PREVIOUS;
    if (fw_contains(l.next,x,y)) return NEXT;
    if (fw_contains(l.font_down,x,y)) return FONT_DOWN;
    if (fw_contains(l.font_up,x,y)) return FONT_UP;
    if (fw_contains(l.face,x,y)) return FACE;
    if (fw_contains(l.sync,x,y)) return SYNC;
    return NOTHING;
}
int fw_scale(int raw, int min, int max, int size) {
    if (max <= min || size <= 0) return 0;
    if (raw < min) raw=min;
    if (raw > max) raw=max;
    return (int)(((int64_t)raw-min)*(size-1)/((int64_t)max-min));
}
void fw_transform(int *x, int *y, int w, int h, bool swap, bool mx, bool my) {
    /* Inputs are normalized to 0..65535, before panel-to-screen transforms. */
    int a=*x, b=*y;
    if (swap) { a=*y; b=*x; }
    if (mx) a=65535-a;
    if (my) b=65535-b;
    *x=fw_scale(a,0,65535,w); *y=fw_scale(b,0,65535,h);
}
bool fw_touch(Touch *t, TouchEvent e, int v, int tolerance, int *x, int *y) {
    if (e == TOUCH_DROPPED) { *t=(Touch){.cancelled=true}; return false; }
    if (e == TOUCH_SLOT) { t->slot=v; return false; }
    if (e == TOUCH_OTHER_CONTACT) { t->cancelled=true; return false; }
    if (e == TOUCH_DOWN) {
        if (!t->down) { t->down=true; t->started=false; t->released=false; }
    } else if (e == TOUCH_UP) { t->down=false; t->released=true; }
    else if (e == TOUCH_X) { t->x=v; t->have_x=true; }
    else if (e == TOUCH_Y) { t->y=v; t->have_y=true; }
    else if (e == TOUCH_FRAME) {
        if (t->down && !t->started && t->have_x && t->have_y) {
            t->start_x=t->x; t->start_y=t->y; t->started=true;
        }
        if (t->started && (abs(t->x-t->start_x)>tolerance || abs(t->y-t->start_y)>tolerance))
            t->cancelled=true;
        if (t->released) {
            bool valid=t->started && t->have_x && t->have_y && !t->cancelled;
            *x=t->x; *y=t->y;
            *t=(Touch){0};
            return valid;
        }
    }
    return false;
}
