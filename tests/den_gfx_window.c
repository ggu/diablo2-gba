#include "../common/gba.h"
#include <stdio.h>
static _Alignas(4) u8 source[512*256];
static const u8 sprite[4]={1,0,2,3};
int main(void) {
    const int lefts[]={-250,-241,-240,-239,-37,-3,-1,0,1,2,3,4,17,394,500,511,512,513};
    const int tops[]={-120,-5,0,1,17,153,255,256};
    gba_init();
    for(int y=0;y<256;y++)for(int x=0;x<512;x++)source[y*512+x]=(u8)((x*19+y*71)^(x>>4));
    for(unsigned a=0;a<sizeof(lefts)/sizeof(lefts[0]);a++)for(unsigned b=0;b<sizeof(tops)/sizeof(tops[0]);b++) {
        int left=lefts[a],top=tops[b];gfx_begin(7);
        if(!gfx_blit_window(14,118,source,512,256,left,top))return 1;
        for(int y=0;y<160;y++)for(int x=0;x<240;x++) {
            int sx=x+left,sy=y-14+top;
            u8 expected=y>=14&&y<132&&sx>=0&&sx<512&&sy>=0&&sy<256?source[sy*512+sx]:7;
            if(gfx_test_pixel(x,y)!=expected){fprintf(stderr,"window mismatch left=%d top=%d at(%d,%d)\n",left,top,x,y);return 2;}
        }
    }
    for(unsigned a=0;a<sizeof(lefts)/sizeof(lefts[0]);a++)for(unsigned b=0;b<sizeof(tops)/sizeof(tops[0]);b++) {
        int left=lefts[a],top=tops[b];gfx_begin(7);
        if(!gfx_blit_window_2x(14,118,source,512,256,left,top))return 4;
        for(int y=0;y<160;y++)for(int x=0;x<240;x++) {
            int sx=(x>>1)+left,sy=((y-14)>>1)+top;
            u8 expected=y>=14&&y<132&&sx>=0&&sx<512&&sy>=0&&sy<256?source[sy*512+sx]:7;
            if(gfx_test_pixel(x,y)!=expected){fprintf(stderr,"2x window mismatch left=%d top=%d at(%d,%d)\n",left,top,x,y);return 5;}
        }
    }
    if(gfx_blit_window(-1,1,source,512,256,0,0)||gfx_blit_window(0,161,source,512,256,0,0)||
       gfx_blit_window(0,1,source+1,512,256,0,0)||gfx_blit_window(0,1,source,511,256,0,0)||
       gfx_blit_window(0,1,0,512,256,0,0))return 3;
    if(gfx_blit_window_2x(-1,2,source,512,256,0,0)||gfx_blit_window_2x(0,3,source,512,256,0,0)||
       gfx_blit_window_2x(0,2,source+1,512,256,0,0)||gfx_blit_window_2x(0,2,source,511,256,0,0)||
       gfx_blit_window_2x(0,2,0,512,256,0,0))return 6;
    const int xpositions[]={-3,-2,-1,0,1,2,237,238,239,240};
    const int ypositions[]={-3,-2,-1,0,1,2,157,158,159,160};
    for(unsigned yi=0;yi<sizeof(ypositions)/sizeof(ypositions[0]);yi++)
    for(unsigned xi=0;xi<sizeof(xpositions)/sizeof(xpositions[0]);xi++) {
        int sx=xpositions[xi],sy=ypositions[yi];gfx_begin(7);
        gfx_blit_sprite_2x(sx,sy,2,2,sprite);
        for(int y=0;y<160;y++)for(int x=0;x<240;x++) {
            u8 expected=7;
            for(int py=0;py<2;py++)for(int px=0;px<2;px++) {
                int left=sx+px*2,top=sy+py*2;
                if(sprite[py*2+px]&&x>=left&&x<left+2&&y>=top&&y<top+2)expected=sprite[py*2+px];
            }
            if(gfx_test_pixel(x,y)!=expected){fprintf(stderr,"2x sprite mismatch left=%d top=%d at(%d,%d)\n",sx,sy,x,y);return 7;}
        }
    }
    puts("PASS: 1x/2x cache windows and transparent sprite expansion clip correctly");
    return 0;
}
