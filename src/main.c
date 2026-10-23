/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "core.h"
#include "process.h"
#include "snapshot.h"
#include "markdown.h"
#include "layout.h"
#include "fbink.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
_Static_assert(sizeof(struct input_event)==16,"Kindle 32-bit input_event ABI required");

typedef struct {
    int fb, touch;
    bool mt, has_slots;
    struct input_absinfo ax, ay;
    FBInkState state;
    FBInkConfig config;
    Layout layout;
    Snapshot snapshot;
    SnapshotDoc document;
    MdDocument blocks;   /* current document as reading blocks */
    FWPos *pages;        /* start of each page computed so far */
    unsigned page_count;
    int document_offset;
    unsigned page;
    int font_size;       /* body px, from size_step */
    int size_step;       /* index into body_sizes */
    int family;          /* 1 = 명조 (serif, 0.7.0 default), 0 = 고딕 */
    int history[32]; int history_len;   /* documents opened before (뒤로) */
    struct { Rect r; size_t block; } hits[64]; int hit_count;   /* link rows on this page */
    bool panel_open;     /* pull-down panel: date, time, battery, Wi-Fi, frontlight */
    int wifi, light, light_max;
} App;
static const int body_sizes[]={42,46,50,54,58,62};
#define BODY_STEPS ((int)(sizeof(body_sizes)/sizeof(body_sizes[0])))
#define BODY_DEFAULT 3   /* 54 px */
/* ---- drawing helpers ------------------------------------------------- */
#define INK 0x00
#define INK_SOFT 0x55     /* folder line, disabled labels */
#define INK_LINE 0xAA     /* separators */
static int fill(App *a, Rect r, unsigned char gray) {
    if(r.w<=0||r.h<=0) return 0;
    FBInkRect rect={(unsigned short)r.x,(unsigned short)r.y,(unsigned short)r.w,(unsigned short)r.h};
    return fbink_fill_rect_gray(a->fb,&a->config,&rect,false,gray);
}
static void pen(unsigned char gray) { (void)fbink_set_fg_pen_gray(gray,false,true); }
/* One line of UI text (고딕); clips instead of failing. align: 0 left, 1 centre, 2 right. */
static int label(App *a, Rect r, int size, bool bold, int align, unsigned char gray, const char *s) {
    FBInkOTConfig ot={0}; FBInkOTFit fit={0};
    ot.margins.top=r.y; ot.margins.bottom=a->layout.height-(r.y+r.h);
    ot.size_px=size; ot.style=bold?FNT_BOLD:FNT_ITALIC;
    int left=r.x;
    if(align==2) {   /* measure, then place flush right */
        ot.margins.left=r.x; ot.margins.right=a->layout.width-(r.x+r.w); ot.compute_only=true;
        if(fbink_print_ot(a->fb,s,&ot,&a->config,&fit)<0) return -1;
        left=r.x+r.w-fit.bbox.width; if(left<r.x) left=r.x; ot.compute_only=false; fit=(FBInkOTFit){0};
    }
    ot.margins.left=left; ot.margins.right=a->layout.width-(r.x+r.w); ot.is_centered=align==1;
    pen(gray); int rc=fbink_print_ot(a->fb,s,&ot,&a->config,&fit); pen(INK);
    if(rc<0) { fprintf(stderr,"label failed rc=%d\n",rc); return -1; }
    return 0;
}
static int text(App *a, Rect r, int size, bool bold, const char *s) { return label(a,r,size,bold,0,INK,s); }
/* Outlined button with a centred label; disabled buttons are drawn grey. */
static int button(App *a, Rect r, const char *name, bool enabled) {
    int t=2; unsigned char edge=enabled?0x33:0xBB;
    if(fill(a,(Rect){r.x,r.y,r.w,t},edge)<0 || fill(a,(Rect){r.x,r.y+r.h-t,r.w,t},edge)<0 ||
       fill(a,(Rect){r.x,r.y,t,r.h},edge)<0 || fill(a,(Rect){r.x+r.w-t,r.y,t,r.h},edge)<0) return -1;
    /* Measure the label, then place its box in the exact middle of the button. */
    int size=a->layout.width/32;
    FBInkOTConfig ot={0}; FBInkOTFit fit={0};
    ot.size_px=size; ot.style=FNT_ITALIC; ot.compute_only=true;
    ot.margins.left=r.x; ot.margins.right=a->layout.width-(r.x+r.w);
    ot.margins.top=r.y; ot.margins.bottom=a->layout.height-(r.y+r.h);
    if(fbink_print_ot(a->fb,name,&ot,&a->config,&fit)<0) return -1;
    int tw=fit.bbox.width, th=fit.bbox.height;
    if(tw<=0||tw>r.w) tw=r.w; if(th<=0||th>r.h) th=r.h;
    int x=r.x+(r.w-tw)/2, y=r.y+(r.h-th)/2;
    ot=(FBInkOTConfig){0}; fit=(FBInkOTFit){0};
    ot.size_px=size; ot.style=FNT_ITALIC;
    ot.margins.left=x; ot.margins.right=a->layout.width-(r.x+r.w-t);
    ot.margins.top=y; ot.margins.bottom=a->layout.height-(r.y+r.h-t);
    pen(enabled?INK:0xAA); int rc=fbink_print_ot(a->fb,name,&ot,&a->config,&fit); pen(INK);
    if(rc<0) { fprintf(stderr,"button failed rc=%d\n",rc); return -1; }
    return 0;
}
static void settings_apply(App *a) {
    if(a->size_step<0) a->size_step=0; if(a->size_step>=BODY_STEPS) a->size_step=BODY_STEPS-1;
    a->family=a->family?1:0; a->font_size=body_sizes[a->size_step];
}
/* Reading settings live in the data directory: KPM replaces the package
   directory on upgrade, the data directory survives it. */
static char settings_path[4200]="state/settings.conf", settings_tmp[4210]="state/settings.tmp";
static void settings_load(App *a) {
    a->size_step=BODY_DEFAULT; a->family=1;   /* 명조 54 px */
    FILE *f=fopen(settings_path,"r");
    if(f) { int step=0,family=0; if(fscanf(f,"body_px=%d\nfamily=%d",&step,&family)==2) { for(int i=0;i<BODY_STEPS;i++) if(body_sizes[i]==step) a->size_step=i; a->family=family; } fclose(f); }
    settings_apply(a);
}
static void settings_save(const App *a) {
    FILE *f=fopen(settings_tmp,"w"); if(!f) return;
    int wrote=fprintf(f,"body_px=%d\nfamily=%d\n",a->font_size,a->family)>0;
    int closed=fclose(f)==0;
    if(wrote&&closed) (void)rename(settings_tmp,settings_path); else (void)unlink(settings_tmp);
}
/* ---- FBInk measuring and drawing for layout.c ---- */
static int ot_setup(App *a,FBInkOTConfig *ot,const FWStyle *st,int x,int y,int width,int height) {
    *ot=(FBInkOTConfig){0};
    ot->margins.left=(short)x; ot->margins.right=(short)(a->layout.width-x-width);
    ot->margins.top=(short)y; ot->margins.bottom=(short)(a->layout.height-y-height);
    /* fonts: REGULAR = Noto Serif CJK SemiBold (명조), ITALIC slot = Noto Sans CJK Medium (고딕), BOLD = Noto Sans CJK Bold */
    ot->size_px=(unsigned short)st->size; ot->style=st->bold?FNT_BOLD:st->family?FNT_REGULAR:FNT_ITALIC;
    return ot->margins.bottom<0||ot->margins.right<0?-1:0;
}
static int ot_measure(void *ctx,const char *s,const FWStyle *st,int width,int available,bool *fits,int *height) {
    App *a=ctx; FBInkOTConfig ot; FBInkOTFit fit={0}; Rect c=a->layout.content;
    if(available>c.h) available=c.h;
    if(ot_setup(a,&ot,st,c.x,c.y,width,available)<0) return -1;
    ot.compute_only=true;
    int rc=fbink_print_ot(a->fb,s,&ot,&a->config,&fit);
    if(rc<0) { fprintf(stderr,"measure failed rc=%d\n",rc); return -1; }
    *fits=!fit.truncated; *height=fit.bbox.height; return 0;
}
static int ot_draw(void *ctx,const char *s,const FWStyle *st,int x,int y,int width,int height) {
    App *a=ctx; FBInkOTConfig ot; FBInkOTFit fit={0}; Rect c=a->layout.content;
    int bottom=y+height+4; if(bottom>c.y+c.h) bottom=c.y+c.h;   /* small slack for rounding */
    if(ot_setup(a,&ot,st,x,y,width,bottom-y)<0) return -1;
    int rc=fbink_print_ot(a->fb,s,&ot,&a->config,&fit);
    if(rc<0) { fprintf(stderr,"text failed rc=%d\n",rc); return -1; }
    if(st->underline) {   /* a linked document: underline every line of the row */
        int lines=fit.rendered_lines?fit.rendered_lines:1, lh=height/lines;
        int w=fit.bbox.width>0&&fit.bbox.width<width?fit.bbox.width:width;
        for(int i=0;i<lines;i++) if(fill(a,(Rect){x,y+(i+1)*lh-lh/8,w,2},0x22)<0) return -1;
    }
    return 0;
}
static int ot_rule(void *ctx,int x,int y,int width) { return fill(ctx,(Rect){x,y,width,2},INK_LINE); }
static int ot_hit(void *ctx,size_t block,int x,int y,int width,int height) {
    App *a=ctx; if(block>=a->blocks.count || !a->blocks.blocks[block].link || a->hit_count>=64) return 0;
    a->hits[a->hit_count].r=(Rect){x,y,width,height}; a->hits[a->hit_count].block=block; a->hit_count++; return 0;
}
static FWArea content_area(const App *a) { Rect c=a->layout.content; return (FWArea){c.x,c.y,c.w,c.h}; }
/* Splits the whole document into pages up front, so the header can show "3 / 12". */
static int paginate(App *a) {
    FWRenderer r={ot_measure,ot_draw,ot_rule,NULL,a};
    free(a->pages); a->pages=malloc(sizeof(FWPos)); if(!a->pages) return -1;
    a->pages[0]=(FWPos){0,0}; a->page_count=1;
    double started=fw_now();
    for(;;) {
        FWPos next; int rc=fw_layout_page(&a->blocks,a->pages[a->page_count-1],&r,content_area(a),a->font_size,a->family,false,&next);
        if(rc<0) return -1;
        if(rc==1 || next.block>=a->blocks.count) break;
        FWPos *grown=realloc(a->pages,(a->page_count+1)*sizeof(FWPos)); if(!grown) return -1;
        a->pages=grown; a->pages[a->page_count++]=next;
        if(a->page_count>2000) break;
    }
    if(a->page>=a->page_count) a->page=a->page_count-1;
    fprintf(stderr,"paginated %s: %u pages in %.0f ms\n",a->document.id,a->page_count,(fw_now()-started)*1000);
    return 0;
}
static int load_document(App *a,int offset) {
    SnapshotDoc next={0}; if(snapshot_load(&a->snapshot,offset,&next)<0) return -1;
    MdDocument blocks={0}; if(markdown_parse(next.body,&blocks)<0) { snapshot_doc_free(&next); return -1; }
    snapshot_doc_free(&a->document); markdown_free(&a->blocks); a->document=next; a->blocks=blocks; a->document_offset=offset;
    a->page=0; return paginate(a);
}
static int draw_page(App *a) {
    FWRenderer r={ot_measure,ot_draw,ot_rule,ot_hit,a}; a->hit_count=0;
    if(!a->blocks.count) return text(a,a->layout.content,a->font_size,false,"(내용이 없는 문서입니다)");
    FWPos next; return fw_layout_page(&a->blocks,a->pages[a->page],&r,content_area(a),a->font_size,a->family,true,&next)<0?-1:0;
}
static int refresh(App *a, bool full) {
    FBInkConfig c=a->config; c.no_refresh=false; c.is_flashing=full;
    /* AUTO delegates supported waveforms to FBInk; no animated or DU text rendering. */
    c.wfm_mode=WFM_AUTO;
    /* Always the whole screen (header shows the page number); only the flash differs. */
    int rc=fbink_refresh(a->fb,0,0,0,0,&c);
    if(rc<0) return rc;
    rc=fbink_wait_for_complete(a->fb,LAST_MARKER);
    if(rc<0 && rc!=-ENOSYS) { fprintf(stderr,"refresh completion failed: %d\n",rc); return rc; }
    return 0;
}
/* ---- system status: clock, battery, Wi-Fi, frontlight (public lipc props) ---- */
/* Runs argv and returns the first line of its output (empty on failure). */
static void capture(char *const argv[], char *out, size_t cap) {
    out[0]=0; int fd[2]; if(pipe(fd)) return;
    pid_t pid=fork(); if(pid<0) { close(fd[0]); close(fd[1]); return; }
    if(!pid) { dup2(fd[1],STDOUT_FILENO); int null=open("/dev/null",O_WRONLY); if(null>=0) dup2(null,STDERR_FILENO); close(fd[0]); close(fd[1]); execv(argv[0],argv); _exit(127); }
    close(fd[1]); ssize_t n=0, got;
    struct pollfd p={fd[0],POLLIN,0}; double end=fw_now()+2;
    while((size_t)n<cap-1 && fw_now()<end && poll(&p,1,200)>=0) { if(!(p.revents&POLLIN)) { if(p.revents) break; continue; } got=read(fd[0],out+n,cap-1-(size_t)n); if(got<=0) break; n+=got; }
    close(fd[0]); out[n]=0; out[strcspn(out,"\r\n")]=0;
    int status; if(waitpid(pid,&status,WNOHANG)==0) { kill(pid,SIGKILL); waitpid(pid,&status,0); }
}
static int prop_get(const char *service,const char *name,int fallback) {
    char out[64]; char *argv[]={"/usr/bin/lipc-get-prop",(char *)service,(char *)name,NULL};
    capture(argv,out,sizeof(out)); char *end; long v=strtol(out,&end,10); return end==out?fallback:(int)v;
}
static void prop_set(const char *service,const char *name,int value) {
    char v[16]; snprintf(v,sizeof(v),"%d",value);
    char *argv[]={"/usr/bin/lipc-set-prop",(char *)service,(char *)name,v,NULL};
    fprintf(stderr,"%s %s=%d -> %d\n",service,name,value,fw_command(argv,3));
}
/* Battery percent from sysfs (any power_supply with type Battery), else lipc. */
static int battery(bool *charging) {
    *charging=false; DIR *d=opendir("/sys/class/power_supply");
    if(d) { struct dirent *e; while((e=readdir(d))) { if(e->d_name[0]=='.') continue;
        char path[300],buf[32]; snprintf(path,sizeof(path),"/sys/class/power_supply/%s/type",e->d_name);
        FILE *f=fopen(path,"r"); if(!f) continue; bool bat=fgets(buf,sizeof(buf),f)&&!strncmp(buf,"Battery",7); fclose(f); if(!bat) continue;
        snprintf(path,sizeof(path),"/sys/class/power_supply/%s/status",e->d_name); f=fopen(path,"r"); if(f) { if(fgets(buf,sizeof(buf),f)) *charging=!strncmp(buf,"Charging",8); fclose(f); }
        snprintf(path,sizeof(path),"/sys/class/power_supply/%s/capacity",e->d_name); f=fopen(path,"r"); int v=-1; if(f) { if(fscanf(f,"%d",&v)!=1) v=-1; fclose(f); }
        if(v>=0) { closedir(d); return v; } } closedir(d); }
    return prop_get("com.lab126.powerd","battLevel",-1);
}
static void clock_text(char *out,size_t cap) {
    time_t now=time(NULL); struct tm t; localtime_r(&now,&t);
    bool charging; int b=battery(&charging);
    if(b>=0) snprintf(out,cap,"%02d:%02d  ·  %s%d%%",t.tm_hour,t.tm_min,charging?"충전 ":"",b);
    else snprintf(out,cap,"%02d:%02d",t.tm_hour,t.tm_min);
}
static void date_text(char *out,size_t cap) {
    static const char *const days[]={"일","월","화","수","목","금","토"};
    time_t now=time(NULL); struct tm t; localtime_r(&now,&t);
    snprintf(out,cap,"%d년 %d월 %d일 (%s)",t.tm_year+1900,t.tm_mon+1,t.tm_mday,days[t.tm_wday]);
}
static Rect clock_rect(const App *a) { int w=a->layout.width,m=w/24,title=w/30; return (Rect){w/3,m*2/3,w*2/3-m,title*3/2}; }
/* Panel geometry: top sheet with five controls. */
typedef struct { Rect sheet, wifi, dim, bright, close; } Panel;
static Panel panel_layout(const App *a) {
    int w=a->layout.width,h=a->layout.height,m=w/24, bh=h/16, gap=m/2;
    Rect sheet={m/2,0,w-m,h*11/25};
    int y=sheet.y+sheet.h-m-bh, bw=(sheet.w-2*m-3*gap)/4;
    Panel p={sheet,{sheet.x+m,y,bw,bh},{sheet.x+m+(bw+gap),y,bw,bh},{sheet.x+m+2*(bw+gap),y,bw,bh},{sheet.x+m+3*(bw+gap),y,bw,bh}};
    return p;
}
static void panel_refresh_state(App *a) {
    a->wifi=prop_get("com.lab126.cmd","wirelessEnable",-1);
    a->light=prop_get("com.lab126.powerd","flIntensity",-1);
    a->light_max=prop_get("com.lab126.powerd","flMaxIntensity",24); if(a->light_max<=0) a->light_max=24;
}
static int draw_panel(App *a) {
    Panel p=panel_layout(a); int w=a->layout.width,m=w/24; Rect s=p.sheet;
    if(fill(a,s,0xFF)<0 || fill(a,(Rect){s.x,s.y+s.h-3,s.w,3},0x33)<0 ||
       fill(a,(Rect){s.x,s.y,3,s.h},0x33)<0 || fill(a,(Rect){s.x+s.w-3,s.y,3,s.h},0x33)<0) return -1;
    char date[64], clock[64], line[160]; date_text(date,sizeof(date)); clock_text(clock,sizeof(clock));
    time_t now=time(NULL); struct tm t; localtime_r(&now,&t); char hm[16]; snprintf(hm,sizeof(hm),"%02d:%02d",t.tm_hour,t.tm_min);
    bool charging; int b=battery(&charging);
    int big=w/10, mid=w/26, y=s.y+m;
    if(label(a,(Rect){s.x+m,y,s.w-2*m,big*3/2},big,true,0,INK,hm)<0) return -1; y+=big*3/2;
    if(label(a,(Rect){s.x+m,y,s.w-2*m,mid*3/2},mid,false,0,INK,date)<0) return -1; y+=mid*2;
    if(b>=0) snprintf(line,sizeof(line),"배터리 %d%%%s  ·  Wi-Fi %s  ·  밝기 %d/%d",b,charging?" 충전 중":"",a->wifi==1?"켜짐":a->wifi==0?"꺼짐":"?",a->light<0?0:a->light,a->light_max);
    else snprintf(line,sizeof(line),"Wi-Fi %s  ·  밝기 %d/%d",a->wifi==1?"켜짐":a->wifi==0?"꺼짐":"?",a->light<0?0:a->light,a->light_max);
    if(label(a,(Rect){s.x+m,y,s.w-2*m,mid*3/2},mid,false,0,INK_SOFT,line)<0) return -1;
    (void)clock;
    if(button(a,p.wifi,a->wifi==1?"Wi-Fi 끄기":"Wi-Fi 켜기",a->wifi>=0)<0 ||
       button(a,p.dim,"밝기 −",a->light>0)<0 || button(a,p.bright,"밝기 +",a->light>=0&&a->light<a->light_max)<0 ||
       button(a,p.close,"닫기",true)<0) return -1;
    return 0;
}
/* Returns 1 to redraw, 0 to ignore. Any tap outside the sheet closes it. */
static int panel_tap(App *a,int x,int y) {
    Panel p=panel_layout(a);
    if(!fw_contains(p.sheet,x,y) || fw_contains(p.close,x,y)) { a->panel_open=false; return 1; }
    if(fw_contains(p.wifi,x,y) && a->wifi>=0) { prop_set("com.lab126.cmd","wirelessEnable",a->wifi?0:1); panel_refresh_state(a); return 1; }
    int step=a->light_max>=12?2:1;
    if(fw_contains(p.dim,x,y) && a->light>0) { prop_set("com.lab126.powerd","flIntensity",a->light-step<0?0:a->light-step); panel_refresh_state(a); return 1; }
    if(fw_contains(p.bright,x,y) && a->light>=0 && a->light<a->light_max) { prop_set("com.lab126.powerd","flIntensity",a->light+step>a->light_max?a->light_max:a->light+step); panel_refresh_state(a); return 1; }
    return 0;
}
static int refresh_rect(App *a,Rect r) {
    FBInkConfig c=a->config; c.no_refresh=false; c.is_flashing=false; c.wfm_mode=WFM_AUTO;
    int rc=fbink_refresh(a->fb,(uint32_t)r.y,(uint32_t)r.x,(uint32_t)r.w,(uint32_t)r.h,&c);
    if(rc<0) return rc; rc=fbink_wait_for_complete(a->fb,LAST_MARKER); return rc<0&&rc!=-ENOSYS?rc:0;
}
/* Minute tick: redraw only the clock in the header (or the whole panel). */
static int tick(App *a) {
    if(a->panel_open) { if(draw_panel(a)<0) return -1; return refresh_rect(a,panel_layout(a).sheet); }
    Rect r=clock_rect(a); char clock[64]; clock_text(clock,sizeof(clock));
    if(fill(a,r,0xFF)<0 || label(a,r,a->layout.width/30,false,2,INK,clock)<0) return -1;
    return refresh_rect(a,r);
}
static int draw(App *a) {
    int w=a->layout.width,m=w/24; Rect c=a->layout.content;
    char page[32], where[600];
    snprintf(page,sizeof(page),"%u / %u",a->page+1,a->page_count);
    snprintf(where,sizeof(where),"%s  ·  %dpx %s",*a->document.directory?a->document.directory:"첫 화면",a->font_size,a->family?"명조":"고딕");
    if(fbink_cls(a->fb,&a->config,NULL,true)<0) return -1;
    int title=w/30, small=w/40, top=m*2/3;
    char clock[64]; clock_text(clock,sizeof(clock));
    if(label(a,(Rect){m,top,w/3,title*3/2},title,true,0,INK,"FinanceWiki")<0 ||
       label(a,clock_rect(a),title,false,2,INK,clock)<0 ||
       label(a,(Rect){m,top+title*3/2+6,w*3/4-m,small*3/2},small,false,0,INK_SOFT,where)<0 ||
       label(a,(Rect){w*3/4,top+title*3/2+6,w/4-m,small*3/2},small,false,2,INK,page)<0 ||
       fill(a,(Rect){m,c.y-m/2,w-2*m,2},INK_LINE)<0) return -1;
    if(draw_page(a)<0) return -1;
    Layout *l=&a->layout;
    if(fill(a,(Rect){m,l->home.y-m/2,w-2*m,2},INK_LINE)<0 ||
       button(a,l->home,"처음",true)<0 || button(a,l->sync,"뒤로",a->history_len>0)<0 ||
       button(a,l->previous,"이전",a->page>0)<0 || button(a,l->next,"다음",a->page+1<a->page_count)<0 ||
       button(a,l->font_down,"A-",a->size_step>0)<0 || button(a,l->font_up,"A+",a->size_step<BODY_STEPS-1)<0 ||
       button(a,l->face,a->family?"고딕":"명조",true)<0 || button(a,l->exit,"종료",true)<0) return -1;
    if(a->panel_open && draw_panel(a)<0) return -1;
    return 0;
}
/* ---- sync progress screen (financewiki --progress < sync output) ---- */
typedef struct { char state[24]; int done, total, all; char title[512]; char message[512]; } Progress;
static const char *progress_message(const Progress *p) {
    if(!strcmp(p->state,"message")) return p->message;
    if(!strcmp(p->state,"checking")) return "업데이트를 확인하는 중";
    if(!strcmp(p->state,"index")) return "문서 목록을 받는 중";
    if(!strcmp(p->state,"plan")) return p->total?"문서를 받는 중":"바뀐 문서가 없어요";
    if(!strcmp(p->state,"doc")) return "문서를 받는 중";
    if(!strcmp(p->state,"uptodate")) return "최신 상태예요";
    if(!strcmp(p->state,"offline")) return "오프라인 · 저장된 문서로 엽니다";
    if(!strcmp(p->state,"done")) return "받기 완료";
    if(!strcmp(p->state,"paused")) return "연결이 끊겼어요 · 받은 문서로 엽니다";
    return "준비하는 중";
}
static int progress_draw(App *a,const Progress *p,bool flash) {
    int w=a->layout.width,h=a->layout.height,m=w/24; Rect c=a->layout.content;
    int title=w/30, big=w/20, small=w/36;
    if(fbink_cls(a->fb,&a->config,NULL,true)<0) return -1;
    if(label(a,(Rect){m,m*2/3,w-2*m,title*3/2},title,true,0,INK,"FinanceWiki")<0 ||
       fill(a,(Rect){m,c.y-m/2,w-2*m,2},INK_LINE)<0) return -1;
    int y=h/3;
    if(label(a,(Rect){m,y,w-2*m,big*3/2},big,true,0,INK,progress_message(p))<0) return -1;
    y+=big*2;
    if(p->total>0) {
        char count[64]; snprintf(count,sizeof(count),"%d / %d",p->done,p->total);
        if(label(a,(Rect){m,y,w-2*m,big*3/2},big,false,0,INK,count)<0) return -1;
        y+=big*2;
        int bw=w-2*m, bh=28, filled=(int)((long)bw*p->done/p->total);
        if(fill(a,(Rect){m,y,bw,2},0x33)<0 || fill(a,(Rect){m,y+bh-2,bw,2},0x33)<0 ||
           fill(a,(Rect){m,y,2,bh},0x33)<0 || fill(a,(Rect){m+bw-2,y,2,bh},0x33)<0 ||
           fill(a,(Rect){m,y,filled,bh},0x33)<0) return -1;
        y+=bh+small*2;
        if(*p->title && label(a,(Rect){m,y,w-2*m,small*3},small,false,0,INK_SOFT,p->title)<0) return -1;
    }
    const char *hint=p->total>0?"바뀐 문서만 받아요. 중간에 끊겨도 받은 문서는 남고, 다음에 이어서 받아요.":"Wi-Fi가 꺼져 있으면 저장된 문서로 엽니다.";
    if(label(a,(Rect){m,h-m*3,w-2*m,small*3},small,false,0,INK_SOFT,hint)<0) return -1;
    return refresh(a,flash);
}
/* Reads the sync output to EOF and draws at most about once a second. Drawing
   errors never stop the reading, so the sync side never blocks on a full pipe. */
static int progress_ui(App *a) {
    Progress p={"",0,0,0,"",""}; char line[2048]; double last=0; bool drawn=false, ok=true;
    while(fgets(line,sizeof(line),stdin)) {
        line[strcspn(line,"\r\n")]=0; bool force=false;
        if(!strncmp(line,"@state ",7)) { snprintf(p.state,sizeof(p.state),"%s",line+7); force=true; }
        else if(!strncmp(line,"@message ",9)) { snprintf(p.state,sizeof(p.state),"message"); snprintf(p.message,sizeof(p.message),"%s",line+9); p.total=0; force=true; }
        else if(!strncmp(line,"@plan ",6)) { int n=0,all=0; if(sscanf(line+6,"%d %d",&n,&all)==2) { p.total=n; p.all=all; p.done=0; snprintf(p.state,sizeof(p.state),"plan"); force=true; } }
        else if(!strncmp(line,"@doc ",5)) { int n=0,t=0,off=0; if(sscanf(line+5,"%d %d %n",&n,&t,&off)>=2) { p.done=n-1; p.total=t; snprintf(p.state,sizeof(p.state),"doc"); snprintf(p.title,sizeof(p.title),"%s",line+5+off); } }
        else continue;
        if(!strcmp(p.state,"done")) p.done=p.total;
        double now=fw_now();
        if(ok && (force || now-last>=1.0)) { if(progress_draw(a,&p,!drawn)<0) ok=false; drawn=true; last=now; }
    }
    return ok?0:1;
}
static int input_open(App *a) {
    size_t count=0;
    FBInkInputDevice *devices=fbink_input_scan(INPUT_TOUCHSCREEN,INPUT_POWER_BUTTON|INPUT_TABLET,NO_RECAP,&count);
    if(!devices) return -1;
    int found=0;
    for(size_t i=0;i<count;i++) {
        int fd=devices[i].fd;
        if(fd<0) continue;
        struct input_id id={0};
        char sys[256],resolved[4096];
        const char *base=strrchr(devices[i].path,'/');
        snprintf(sys,sizeof(sys),"/sys/class/input/%s/device",base?base+1:"");
        bool physical=realpath(sys,resolved) && !strstr(resolved,"/virtual/");
        if(!devices[i].matched || ioctl(fd,EVIOCGID,&id)<0 || !physical ||
           id.bustype==BUS_BLUETOOTH || id.bustype==BUS_USB || id.bustype==BUS_VIRTUAL) { close(fd); continue; }
        struct input_absinfo x,y,slot;
        bool mt=ioctl(fd,EVIOCGABS(ABS_MT_POSITION_X),&x)==0 && ioctl(fd,EVIOCGABS(ABS_MT_POSITION_Y),&y)==0;
        if(!mt && (ioctl(fd,EVIOCGABS(ABS_X),&x)<0 || ioctl(fd,EVIOCGABS(ABS_Y),&y)<0)) { close(fd); continue; }
        if(x.maximum<=x.minimum || y.maximum<=y.minimum) { close(fd); continue; }
        found++;
        if(found==1) {
            a->touch=fd; a->ax=x; a->ay=y; a->mt=mt;
            a->has_slots=ioctl(fd,EVIOCGABS(ABS_MT_SLOT),&slot)==0;
            fprintf(stderr,"touch=%s name=%s bus=%u mt=%d slots=%d ranges=%d..%d,%d..%d\n",
                devices[i].path,devices[i].name,id.bustype,mt,a->has_slots,x.minimum,x.maximum,y.minimum,y.maximum);
        } else close(fd);
    }
    free(devices);
    if(found!=1) { fprintf(stderr,"expected one physical panel, found %d\n",found); return -1; }
    return 0;
}
static int start_offset(App *a) { int offset=snapshot_offset_of(&a->snapshot,"index.md"); return offset<0?0:offset; }
/* Opens a document and remembers the current one for 뒤로. */
static int open_document(App *a,int offset) {
    if(offset==a->document_offset) { a->page=0; return 0; }
    if(a->history_len==32) { memmove(a->history,a->history+1,sizeof(a->history[0])*31); a->history_len--; }
    a->history[a->history_len++]=a->document_offset;
    return load_document(a,offset);
}
/* A tap on a link row ("›") inside the page opens that document. */
static int content_tap(App *a,int x,int y) {
    for(int i=0;i<a->hit_count;i++) {
        Rect r=a->hits[i].r; if(!fw_contains(r,x,y)) continue;
        int offset=snapshot_resolve(&a->snapshot,a->blocks.blocks[a->hits[i].block].link,a->document.id);
        fprintf(stderr,"link %s -> %d\n",a->blocks.blocks[a->hits[i].block].link,offset);
        if(offset<0) return 0;
        return open_document(a,offset)<0?-1:1;
    }
    return 0;
}
static int ui(App *a) {
    if(ioctl(a->touch,EVIOCGRAB,1)<0) { perror("touch grab"); return 1; }
    /* Discard the launcher tap. Fail closed on a disconnected/overflowed device. */
    struct input_event events[32];
    while(read(a->touch,events,sizeof(events))>0) {}
    if(draw(a)<0 || refresh(a,true)<0) { fputs("Initial draw failed.\n",stderr); return 1; }
    { char clock[64], date[64]; clock_text(clock,sizeof(clock)); date_text(date,sizeof(date)); fprintf(stderr,"clock %s %s (TZ=%s)\n",date,clock,getenv("TZ")?getenv("TZ"):"unset"); }
    fputs("Initial draw complete; waiting for touch.\n",stderr);
    unsigned updates=0;
    Touch touch={0};
    /* Swipe tracking (fw_touch only reports stationary taps). */
    struct { int x,y,sx,sy; bool down,started,have_x,have_y,up; } g={0};
    double last_input=fw_now();
    for(;;) {
        struct pollfd p={a->touch,POLLIN,0};
        /* Wake at the next minute for the clock, or at the 15-minute idle limit. */
        double now=fw_now(); time_t wall=time(NULL);
        int to_minute=(int)(60-wall%60)*1000+50, to_idle=(int)((last_input+15*60-now)*1000);
        if(to_idle<=0) { fputs("Idle timeout; closing the reader.\n",stderr); return 0; }
        int rc=poll(&p,1,to_minute<to_idle?to_minute:to_idle);
        if(rc<0 && errno==EINTR) continue;
        if(rc==0) { if(fw_now()-last_input>=15*60) { fputs("Idle timeout; closing the reader.\n",stderr); return 0; } if(tick(a)<0) return 1; continue; }
        if(rc<0 || p.revents&(POLLERR|POLLHUP|POLLNVAL)) return 1;
        last_input=fw_now();
        ssize_t n=read(a->touch,events,sizeof(events));
        if(n<0 && (errno==EAGAIN || errno==EINTR)) continue;
        if(n<=0 || n%(ssize_t)sizeof(events[0])) return 1;
        for(size_t i=0;i<(size_t)n/sizeof(events[0]);i++) {
            struct input_event e=events[i]; TouchEvent kind; int val=e.value,x,y;
            if(e.type==EV_SYN && e.code==SYN_DROPPED) return 1;
            if(e.type==EV_ABS && e.code==ABS_MT_SLOT) { touch.slot=val; continue; }
            if(a->has_slots && touch.slot!=0 && e.type==EV_ABS) {
                if(e.code==ABS_MT_TRACKING_ID && val>=0) touch.cancelled=true;
                continue;
            }
            if(e.type==EV_ABS && e.code==(a->mt?ABS_MT_POSITION_X:ABS_X)) {
                kind=TOUCH_X; val=fw_scale(val,a->ax.minimum,a->ax.maximum,65536);
            } else if(e.type==EV_ABS && e.code==(a->mt?ABS_MT_POSITION_Y:ABS_Y)) {
                kind=TOUCH_Y; val=fw_scale(val,a->ay.minimum,a->ay.maximum,65536);
            } else if(e.type==EV_ABS && e.code==ABS_MT_TRACKING_ID) kind=val<0?TOUCH_UP:TOUCH_DOWN;
            else if(e.type==EV_KEY && e.code==BTN_TOUCH && !a->has_slots) kind=val?TOUCH_DOWN:TOUCH_UP;
            else if(e.type==EV_SYN && e.code==SYN_REPORT) kind=TOUCH_FRAME;
            else continue;
            /* swipe: remember where a contact started and ended */
            if(kind==TOUCH_X) { g.x=val; g.have_x=true; } else if(kind==TOUCH_Y) { g.y=val; g.have_y=true; }
            else if(kind==TOUCH_DOWN) { g.down=true; g.started=false; g.up=false; }
            else if(kind==TOUCH_UP) g.up=true;
            else if(kind==TOUCH_FRAME) {
                if(g.down && !g.started && g.have_x && g.have_y) { g.sx=g.x; g.sy=g.y; g.started=true; }
                if(g.up && g.started) {
                    int x0=g.sx,y0=g.sy,x1=g.x,y1=g.y; g.down=g.started=g.up=false;
                    fw_transform(&x0,&y0,a->layout.width,a->layout.height,a->state.touch_swap_axes,a->state.touch_mirror_x,a->state.touch_mirror_y);
                    fw_transform(&x1,&y1,a->layout.width,a->layout.height,a->state.touch_swap_axes,a->state.touch_mirror_x,a->state.touch_mirror_y);
                    int dy=y1-y0, limit=a->layout.height/10;
                    bool open_swipe=!a->panel_open && y0<a->layout.content.y && dy>limit;
                    bool close_swipe=a->panel_open && dy<-limit;
                    if(open_swipe || close_swipe) {
                        fprintf(stderr,"swipe %s y0=%d dy=%d\n",open_swipe?"down":"up",y0,dy);
                        a->panel_open=open_swipe; if(open_swipe) panel_refresh_state(a);
                        (void)fw_touch(&touch,kind,val,1400,&x,&y);   /* keep the tap tracker in step */
                        updates++; if(draw(a)<0 || refresh(a,updates%6==0)<0) return 1;
                        continue;
                    }
                } else if(g.up) g.down=g.started=g.up=false;
            }
            if(!fw_touch(&touch,kind,val,1400,&x,&y)) continue;
            /* Axis ranges describe sensor units, not display orientation.
               Do not infer an extra swap from their aspect ratio or fb rotate. */
            fw_transform(&x,&y,a->layout.width,a->layout.height,a->state.touch_swap_axes,
                         a->state.touch_mirror_x,a->state.touch_mirror_y);
            Action action=fw_action(a->layout,x,y);
            fprintf(stderr,"tap x=%d y=%d action=%d panel=%d\n",x,y,action,a->panel_open);
            if(a->panel_open) {   /* the sheet takes every tap while it is open */
                if(!panel_tap(a,x,y)) continue;
                updates++; if(draw(a)<0 || refresh(a,updates%6==0)<0) return 1; continue;
            }
            if(fw_contains(clock_rect(a),x,y)) { a->panel_open=true; panel_refresh_state(a); updates++; if(draw(a)<0 || refresh(a,updates%6==0)<0) return 1; continue; }
            if(action==EXIT_APP) return 0;
            if(action==HOME) { if(open_document(a,start_offset(a))<0) return 1; }
            else if(action==PREVIOUS) { if(!a->page) continue; a->page--; }   /* pages of this document only */
            else if(action==NEXT) { if(a->page+1>=a->page_count) continue; a->page++; }
            else if(action==FONT_DOWN) { if(a->size_step<=0) continue; a->size_step--; settings_apply(a); a->page=0; if(paginate(a)<0) return 1; settings_save(a); }
            else if(action==FONT_UP) { if(a->size_step>=BODY_STEPS-1) continue; a->size_step++; settings_apply(a); a->page=0; if(paginate(a)<0) return 1; settings_save(a); }
            else if(action==FACE) { a->family=!a->family; settings_apply(a); a->page=0; if(paginate(a)<0) return 1; settings_save(a); }
            else if(action==SYNC) { /* labelled 뒤로: previous document */ if(!a->history_len) continue; if(load_document(a,a->history[--a->history_len])<0) return 1; }
            else if(action==NOTHING && fw_contains(a->layout.content,x,y)) { int rc=content_tap(a,x,y); if(rc<0) return 1; if(!rc) continue; }
            else continue;
            /* e-ink: a full flash every 6th update clears ghosting; the rest refresh without flashing */
            updates++; if(draw(a)<0 || refresh(a,updates%6==0)<0) return 1;
        }
    }
}
static pid_t power_open(int *readfd) {
    int p[2];
    if(pipe2(p,O_CLOEXEC)<0) return -1;
    pid_t pid=fork();
    if(pid==0) {
        fw_child_signals(); prctl(PR_SET_PDEATHSIG,SIGTERM);
        if(getppid()==1) _exit(1);
        dup2(p[1],STDOUT_FILENO); close(p[0]); close(p[1]);
        execl("/usr/bin/lipc-wait-event","lipc-wait-event","-m","-s","0",
            "com.lab126.powerd","goingToScreenSaver",(char *)NULL);
        _exit(127);
    }
    close(p[1]);
    if(pid<0) { close(p[0]); return -1; }
    *readfd=p[0]; fcntl(*readfd,F_SETFL,O_NONBLOCK); return pid;
}
static void recover_orphan(void) {
    /* Launcher died (including SIGKILL). Restore only its recorded ownership. */
    fw_reset_stop();
    const char *value=getenv("FW_AWESOME_PID");
    if(value) {
        char *end; long pid=strtol(value,&end,10);
        if(!*end && pid>1) {
            char path[80],name[32]={0}; snprintf(path,sizeof(path),"/proc/%ld/comm",pid);
            FILE *f=fopen(path,"r");
            if(f) { (void)fgets(name,sizeof(name),f); fclose(f); }
            if(!strcmp(name,"awesome\n")) (void)kill((pid_t)pid,SIGCONT);
        }
    }
    value=getenv("FW_PILLOW_OWNED");
    if(value && !strcmp(value,"1")) {
        char *args[]={"/usr/bin/lipc-set-prop","com.lab126.pillow","disableEnablePillow","enable",NULL};
        fprintf(stderr,"orphan pillow restore=%d\n",fw_command(args,3));
    }
    value=getenv("FW_SCREENSAVER_OWNED");
    if(value && !strcmp(value,"1")) {
        char *args[]={"/usr/bin/lipc-set-prop","com.lab126.powerd","preventScreenSaver","0",NULL};
        fprintf(stderr,"orphan screensaver restore=%d\n",fw_command(args,3));
    }
    value=getenv("FW_STATUSBAR_OWNED");
    if(value && !strcmp(value,"1")) {
        char *args[]={"/sbin/start","statusbar",NULL};
        fprintf(stderr,"orphan statusbar restore=%d\n",fw_command(args,3));
    }
    (void)rmdir("/var/tmp/financewiki-phase2.lock");
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--version")) { puts(FW_VERSION); return 0; }
    fw_signals();
    if(argc>=3 && !strcmp(argv[1],"--bounded-command")) return fw_command(argv+2,3);
    bool probe=argc==2 && !strcmp(argv[1],"--probe");
    bool progress=argc==2 && !strcmp(argv[1],"--progress");
    if(progress) {   /* sync screen: needs the display and fonts only */
        App a={.fb=-1,.touch=-1}; int rc=1;
        a.config.no_viewport=true; a.config.no_refresh=true; a.config.fg_color=FG_BLACK; a.config.bg_color=BG_WHITE;
        a.fb=fbink_open();
        if(a.fb>=0 && fbink_init(a.fb,&a.config)>=0) {
            fbink_get_state(&a.config,&a.state); a.layout=fw_layout(a.state.view_width,a.state.view_height);
            if(fw_supported_display(a.state.device_name,a.state.is_mtk,a.state.current_rota,a.state.view_width,a.state.view_height,a.state.bpp) &&
               fbink_add_ot_font("fonts/NotoSansCJKkr-Medium.ttf",FNT_ITALIC)>=0 && fbink_add_ot_font("fonts/NotoSansCJKkr-Bold.otf",FNT_BOLD)>=0)
                rc=progress_ui(&a);
        }
        char sink[4096]; while(fgets(sink,sizeof(sink),stdin)) {}   /* always drain the pipe */
        (void)fbink_free_ot_fonts(); if(a.fb>=0) fbink_close(a.fb);
        return rc;
    }
    if(argc>1 && !probe) { fputs("usage: financewiki [--version|--probe|--progress]\n",stderr); return 2; }
    if(!probe && (!getenv("FW_LAUNCHED") || strcmp(getenv("FW_LAUNCHED"),"1"))) {
        fputs("Use launch.sh so Kindle UI ownership is restored.\n",stderr); return 2;
    }
    pid_t launcher=getppid();
    if(!probe) {
        if(prctl(PR_SET_PDEATHSIG,SIGTERM)<0) return 1;
        if(getppid()!=launcher || launcher==1) { recover_orphan(); return 1; }
    }
    App a={.fb=-1,.touch=-1}; int result=1;
    a.config.no_viewport=true; a.config.no_refresh=true;
    a.config.fg_color=FG_BLACK; a.config.bg_color=BG_WHITE;
    a.fb=fbink_open();
    if(a.fb<0 || fbink_init(a.fb,&a.config)<0) goto done;
    fcntl(a.fb,F_SETFD,FD_CLOEXEC);
    fbink_get_state(&a.config,&a.state);
    fprintf(stderr,"FinanceWiki %s device=%s code=%s platform=%s size=%ux%u bpp=%u rotation=%u\n",
        FW_VERSION,a.state.device_name,a.state.device_codename,a.state.device_platform,
        a.state.view_width,a.state.view_height,a.state.bpp,a.state.current_rota);
    /* Phase 2 deliberately fails closed on other models / orientation. */
    if(!fw_supported_display(a.state.device_name,a.state.is_mtk,a.state.current_rota,
                            a.state.view_width,a.state.view_height,a.state.bpp)) {
        fputs("Unsupported display: expected Basic 5/MTK, 1072x1448, 8bpp, native rotation=3.\n",stderr); goto done;
    }
    fprintf(stderr,"Display accepted; touch mapping swap=%d mirror_x=%d mirror_y=%d\n",
            a.state.touch_swap_axes,a.state.touch_mirror_x,a.state.touch_mirror_y);
    a.layout=fw_layout(a.state.view_width,a.state.view_height);
    /* The 0.7.0 faces at e-ink weights (tools/make_fonts.py) plus a real bold for headings. */
    if(fbink_add_ot_font("fonts/NotoSerifCJKkr-SemiBold.ttf",FNT_REGULAR)<0 ||
       fbink_add_ot_font("fonts/NotoSansCJKkr-Medium.ttf",FNT_ITALIC)<0 ||
       fbink_add_ot_font("fonts/NotoSansCJKkr-Bold.otf",FNT_BOLD)<0 || input_open(&a)<0) goto done;
    const char *data_dir=getenv("FINANCEWIKI_DATA_DIR");
    if(data_dir && *data_dir) { snprintf(settings_path,sizeof(settings_path),"%s/settings.conf",data_dir); snprintf(settings_tmp,sizeof(settings_tmp),"%s/settings.tmp",data_dir); }
    settings_load(&a);
    char database[4096];
    if(!data_dir || !*data_dir) data_dir="data";
    if(snprintf(database,sizeof(database),"%s/financewiki.db",data_dir)>=(int)sizeof(database) || snapshot_open(&a.snapshot,database)<0 || !a.snapshot.count || load_document(&a,start_offset(&a))<0) {
        fputs("Cannot open a valid read-only FinanceWiki snapshot.\n",stderr); goto done;
    }
    fprintf(stderr,"Snapshot ready: documents=%d first=%s\n",a.snapshot.count,a.document.title);
    fputs("Fonts and physical touch ready.\n",stderr);
    if(probe) { fputs("Probe passed.\n",stderr); result=0; goto done; }
    FBInkDump previous={0};
    if(fbink_dump(a.fb,&previous)<0) goto done;
    int power_fd=-1; pid_t power=power_open(&power_fd);
    if(power<0) { fbink_free_dump_data(&previous); goto done; }
    pid_t child=fork();
    if(child==0) {
        fw_child_signals(); prctl(PR_SET_PDEATHSIG,SIGTERM);
        if(getppid()==1) _exit(1);
        close(power_fd);
        _exit(ui(&a));
    }
    close(a.touch); a.touch=-1;
    fprintf(stderr,"supervisor_pid=%ld viewer_pid=%ld limit=none idle=900s\n",(long)getpid(),(long)child);
    int asleep=0;
    result=child<0?125:fw_wait(child,0,power_fd,&asleep);
    fprintf(stderr,"viewer exit=%d screensaver=%d\n",result,asleep);
    kill(power,SIGTERM);
    int dummy=0; fw_wait(power,2,-1,&dummy); close(power_fd);
    if(!asleep) {
        a.config.no_refresh=false; a.config.is_flashing=true;
        if(fbink_reinit(a.fb,&a.config)<0 || fbink_restore(a.fb,&a.config,&previous)<0) result=125;
        else (void)fbink_wait_for_complete(a.fb,LAST_MARKER);
    }
    fbink_free_dump_data(&previous);
done:
    markdown_free(&a.blocks);
    free(a.pages);
    snapshot_doc_free(&a.document);
    snapshot_close(&a.snapshot);
    if(a.touch>=0) close(a.touch);
    (void)fbink_free_ot_fonts();
    if(a.fb>=0) fbink_close(a.fb);
    if(!probe && getppid()!=launcher) recover_orphan();
    return result;
}
