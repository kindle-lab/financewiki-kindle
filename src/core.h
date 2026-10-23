/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FW_CORE_H
#define FW_CORE_H
#include <stdbool.h>
#define FW_VERSION "1.1.0"
typedef struct { int x, y, w, h; } Rect;
typedef enum { NOTHING, HOME, PREVIOUS, NEXT, FONT_DOWN, FONT_UP, FACE, SYNC, EXIT_APP } Action;
typedef struct { int width, height; Rect content, home, previous, next, font_down, font_up, face, sync, exit; } Layout;
typedef struct {
    int x, y, start_x, start_y, slot;
    bool down, started, released, have_x, have_y, cancelled;
} Touch;
typedef enum { TOUCH_X, TOUCH_Y, TOUCH_SLOT, TOUCH_DOWN, TOUCH_UP,
               TOUCH_FRAME, TOUCH_DROPPED, TOUCH_OTHER_CONTACT } TouchEvent;
extern const char *fw_paragraph;
extern const char *fw_symbols;
bool fw_supported_display(const char *name, bool mtk, unsigned rotation,
                          unsigned width, unsigned height, unsigned bpp);
Layout fw_layout(int width, int height);
bool fw_contains(Rect r, int x, int y);
Action fw_action(Layout l, int x, int y);
/* Returns true only for a complete, stationary, single-contact tap. */
bool fw_touch(Touch *t, TouchEvent event, int value, int tolerance, int *x, int *y);
int fw_scale(int raw, int min, int max, int size);
void fw_transform(int *x, int *y, int w, int h, bool swap, bool mx, bool my);
#endif
