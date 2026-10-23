/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "core.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
static int check(stbtt_fontinfo *font,const char *s) {
    const unsigned char *p=(const unsigned char *)s;
    int missing=0;
    while(*p) {
        uint32_t cp=*p++;
        if(cp>=0xf0) { cp&=7; for(int i=0;i<3;i++) cp=cp*64+(*p++&63); }
        else if(cp>=0xe0) { cp&=15; for(int i=0;i<2;i++) cp=cp*64+(*p++&63); }
        else if(cp>=0xc0) { cp&=31; cp=cp*64+(*p++&63); }
        if(cp>=32 && !stbtt_FindGlyphIndex(font,cp)) { fprintf(stderr,"missing U+%04X\n",cp); missing++; }
    }
    return missing;
}
int main(int argc,char **argv) {
    if(argc!=2) return 2;
    FILE *f=fopen(argv[1],"rb"); if(!f) return 1;
    fseek(f,0,SEEK_END); long n=ftell(f); rewind(f);
    if(n<=0) return 1;
    unsigned char *bytes=malloc((size_t)n);
    if(!bytes || fread(bytes,1,(size_t)n,f)!=(size_t)n) return 1;
    fclose(f); stbtt_fontinfo font;
    if(!stbtt_InitFont(&font,bytes,0)) return 1;
    int missing=check(&font,fw_symbols)+check(&font,fw_paragraph)+check(&font,"FinanceWiki 부분 갱신 전체 종료 시험 읽기 전용 Version");
    for(int cp=0xac00;cp<=0xd7a3;cp++) if(!stbtt_FindGlyphIndex(&font,cp)) missing++;
    /* Exercise the exact rasterizer FBInk uses, not just font metadata. */
    int w,h,x,y;
    unsigned char *glyph=stbtt_GetCodepointBitmap(&font,0,stbtt_ScaleForPixelHeight(&font,38),0xd55c,&w,&h,&x,&y);
    int ink=0; if(glyph) for(int i=0;i<w*h;i++) ink+=glyph[i]>0;
    free(glyph); free(bytes);
    printf("%s: 11172 Hangul syllables + UI text, missing=%d; rasterized '한'=%dx%d ink=%d\n",argv[1],missing,w,h,ink);
    return missing || !ink;
}
