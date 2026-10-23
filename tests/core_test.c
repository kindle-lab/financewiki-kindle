/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "core.h"
#include <assert.h>
#include <stdio.h>
static bool event(Touch *t,TouchEvent e,int v) { int x,y; return fw_touch(t,e,v,20,&x,&y); }
int main(void) {
    /* Regression: the real KT6 reports rotation=3 with a portrait buffer. */
    assert(fw_supported_display("Basic 5",true,3,1072,1448,8));
    for(unsigned r=0;r<3;r++) assert(!fw_supported_display("Basic 5",true,r,1072,1448,8));
    assert(!fw_supported_display("Basic 5",true,4,1072,1448,8));
    assert(!fw_supported_display("Basic 5",true,3,1448,1072,8));
    assert(!fw_supported_display("Basic 5",true,3,1072,1448,16));
    assert(!fw_supported_display("Basic 5",true,3,0,1448,8));
    assert(!fw_supported_display("Basic 4",true,3,1072,1448,8));
    assert(!fw_supported_display("Basic 5",false,3,1072,1448,8));
    assert(!fw_supported_display(NULL,true,3,1072,1448,8));
    Layout l=fw_layout(1072,1448);
    assert(fw_action(l,l.exit.x,l.exit.y)==EXIT_APP);
    assert(fw_action(l,l.exit.x+l.exit.w,l.exit.y)==NOTHING);
    assert(fw_action(l,l.home.x+1,l.home.y+1)==HOME);
    assert(fw_action(l,l.previous.x+1,l.previous.y+1)==PREVIOUS);
    assert(fw_action(l,l.next.x+1,l.next.y+1)==NEXT);
    assert(fw_action(l,l.font_down.x+1,l.font_down.y+1)==FONT_DOWN);
    assert(fw_action(l,l.font_up.x+1,l.font_up.y+1)==FONT_UP);
    assert(fw_action(l,l.face.x+1,l.face.y+1)==FACE);
    assert(fw_action(l,l.sync.x+1,l.sync.y+1)==SYNC);
    assert(fw_action(l,0,0)==NOTHING);
    assert(fw_scale(-1,0,4095,1072)==0);
    assert(fw_scale(5000,0,4095,1072)==1071);
    assert(fw_scale(1,1,1,1072)==0);
    int x=0,y=65535;
    fw_transform(&x,&y,1072,1448,true,true,false);
    assert(x==0 && y==0);
    /* Native rotation=3 must not rotate the already portrait coordinates.
       Sensor X may have more units than Y: that does not mean swapped axes. */
    for(int corner=0;corner<4;corner++) {
        x=fw_scale((corner&1)?4095:0,0,4095,65536);
        y=fw_scale((corner&2)?2047:0,0,2047,65536);
        fw_transform(&x,&y,1072,1448,false,false,false);
        assert(x==((corner&1)?1071:0) && y==((corner&2)?1447:0));
    }
    Rect buttons[]={l.home,l.previous,l.next,l.font_down,l.font_up,l.face,l.sync,l.exit};
    Action actions[]={HOME,PREVIOUS,NEXT,FONT_DOWN,FONT_UP,FACE,SYNC,EXIT_APP};
    for(int i=0;i<8;i++) {
        x=fw_scale(buttons[i].x+buttons[i].w/2,0,1071,65536);
        y=fw_scale(buttons[i].y+buttons[i].h/2,0,1447,65536);
        fw_transform(&x,&y,1072,1448,false,false,false);
        assert(fw_action(l,x,y)==actions[i]);
    }
    Touch t={0};
    event(&t,TOUCH_X,100); event(&t,TOUCH_Y,200);
    event(&t,TOUCH_DOWN,0); assert(!event(&t,TOUCH_FRAME,0));
    event(&t,TOUCH_UP,0); assert(event(&t,TOUCH_FRAME,0));
    assert(!event(&t,TOUCH_FRAME,0)); /* no repeated tap */
    event(&t,TOUCH_UP,0); assert(!event(&t,TOUCH_FRAME,0)); /* orphan release */
    event(&t,TOUCH_DOWN,0); event(&t,TOUCH_X,100); event(&t,TOUCH_Y,100);
    event(&t,TOUCH_FRAME,0); event(&t,TOUCH_X,500); event(&t,TOUCH_FRAME,0);
    event(&t,TOUCH_X,100); event(&t,TOUCH_UP,0);
    assert(!event(&t,TOUCH_FRAME,0)); /* move away and back is still not a tap */
    event(&t,TOUCH_DOWN,0); event(&t,TOUCH_X,1); event(&t,TOUCH_Y,1);
    event(&t,TOUCH_FRAME,0); event(&t,TOUCH_OTHER_CONTACT,0);
    event(&t,TOUCH_UP,0); assert(!event(&t,TOUCH_FRAME,0));
    event(&t,TOUCH_DOWN,0); event(&t,TOUCH_DROPPED,0);
    event(&t,TOUCH_UP,0); assert(!event(&t,TOUCH_FRAME,0));
    puts("core: KT6 display guard, portrait coordinates, layout, bounds, taps, swipes, multitouch and dropped-event checks passed");
}
