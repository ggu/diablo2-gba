#include "gba.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int check_glyph(int x,int y) {
 static const u8 rows[7]={14,17,17,31,17,17,17};
 gfx_begin(7);gfx_text(x,y,"a",5);
 for(int py=0;py<160;py++)for(int px=0;px<240;px++) {
  u8 expected=7;
  int row=py-y,col=px-x;
  if(row>=0&&row<7&&col>=0&&col<5&&(rows[row]&(16>>col)))expected=5;
  if(gfx_test_pixel(px,py)!=expected)return 0;
 }
 return 1;
}
static int check_band(int y,int height) {
 _Alignas(4) u8 saved[240*40],captured[240*40];
 if(height>40)return 0;
 for(int row=0;row<height;row++)for(int x=0;x<240;x++)
  saved[row*240+x]=(u8)(x*13+(row+y)*7+((x^(row+y))>>2));
 gfx_begin(9);
 if(!gfx_blit_band(y,height,saved)||!gfx_read_band(y,height,captured)||memcmp(saved,captured,height*240))return 0;
 /* Repaint everything, restore only the saved rows, and ensure adjacent rows
    were not touched. This also exercises the first and final visible rows. */
 gfx_begin(33);
 if(!gfx_blit_band(y,height,captured))return 0;
 for(int py=0;py<160;py++)for(int x=0;x<240;x++) {
  u8 expected=py>=y&&py<y+height?saved[(py-y)*240+x]:33;
  if(gfx_test_pixel(x,py)!=expected)return 0;
 }
 return 1;
}
int main(int argc,char**argv){
 gba_init();
 /* Imported terrain must retain pixel alignment and clip at every edge. */
 {
  _Alignas(4) u8 tile[256];
  const int positions[][2]={{0,0},{1,1},{2,3},{3,2},{4,3},{6,5},{7,7},{-7,-5},{-15,155},{232,152},{239,159},{-16,0},{240,0}};
  for(int i=0;i<256;i++)tile[i]=(u8)(32+i%224);
  for(unsigned p=0;p<sizeof(positions)/sizeof(positions[0]);p++){
   int ox=positions[p][0],oy=positions[p][1];gfx_begin(0);gfx_blit16(ox,oy,tile);
   for(int y=0;y<160;y++)for(int x=0;x<240;x++){
    u8 expected=x>=ox&&x<ox+16&&y>=oy&&y<oy+16?tile[(y-oy)*16+x-ox]:0;
    if(gfx_test_pixel(x,y)!=expected){fputs("Imported tile clipping/alignment failed\n",stderr);return 1;}
   }
   for(int i=0;i<256;i++)tile[i]=(u8)(i%5?32+i%224:0);
   gfx_begin(7);gfx_blit_sprite(ox,oy,16,16,tile);
   for(int y=0;y<160;y++)for(int x=0;x<240;x++){
    u8 src=x>=ox&&x<ox+16&&y>=oy&&y<oy+16?tile[(y-oy)*16+x-ox]:0;
    if(gfx_test_pixel(x,y)!=(src?src:7)){fputs("Sprite transparency/clipping failed\n",stderr);return 1;}
   }
   for(int i=0;i<256;i++)tile[i]=(u8)(32+i%224);
  }
 }
 /* Packed glyph writes must preserve the original 5x7 mask at each
    halfword/word alignment and clip identically at all screen edges. */
 {
  const int positions[][2]={{0,0},{1,1},{2,3},{3,4},{-4,-3},{236,157},{239,159},{-5,1}};
  for(unsigned i=0;i<sizeof(positions)/sizeof(positions[0]);i++)
   if(!check_glyph(positions[i][0],positions[i][1])){
    fputs("Packed glyph alignment/clipping failed\n",stderr);return 1;
   }
 }
 /* Framebuffer band DMA/cache API: aligned bounded bands, exact pixels,
    unchanged rows outside the copied range, and invalid inputs rejected. */
 {
  _Alignas(4) u8 scratch[240*40];
  memset(scratch,0x6d,sizeof(scratch));
  if(gfx_read_band(0,1,scratch+1)||gfx_blit_band(0,1,scratch+1)||
     gfx_read_band(-1,1,scratch)||gfx_blit_band(0,0,scratch)||
     gfx_read_band(159,2,scratch)||gfx_blit_band(160,1,scratch)||
     gfx_read_band(0,1,NULL)||gfx_blit_band(0,1,NULL)||
     scratch[0]!=0x6d||scratch[1]!=0x6d) {
   fputs("Framebuffer band bounds/alignment validation failed\n",stderr);return 1;
  }
  const int bands[][2]={{0,1},{12,12},{120,40},{159,1}};
  for(unsigned i=0;i<sizeof(bands)/sizeof(bands[0]);i++)
   if(!check_band(bands[i][0],bands[i][1])){
    fputs("Framebuffer band pixel parity failed\n",stderr);return 1;
   }
 }
 unsigned char sample[16], restored[16];
 for(int i=0;i<16;i++)sample[i]=(unsigned char)(i*17);
 save_write(sample,16,0x54455354u);
 if(!save_read(restored,16,0x54455354u)||memcmp(sample,restored,16)||save_read(restored,15,0x54455354u)||save_read(restored,16,0x54455355u)){
  fputs("SRAM record validation failed\n",stderr);return 1;
 }
 sample[0]=42;
 save_write(sample,16,0x54455354u);
 if(!save_read(restored,16,0x54455354u)||restored[0]!=42)return 1;
 save_test_corrupt_latest(16,0x54455354u);
 if(!save_read(restored,16,0x54455354u)||restored[0]!=0){
  fputs("SRAM fallback to previous valid record failed\n",stderr);return 1;
 }
 int errors=game_self_test();
 if(errors){fprintf(stderr,"Game rule self-tests failed: %d\n",errors);return 1;}
 /* Exercise exact renderer with memory instrumentation, then run input stress. */
 game_init();game_render();
 if(argc>1){char p[512];snprintf(p,sizeof(p),"%s-title.ppm",argv[1]);gfx_dump_ppm(p);}
 game_update(KEY_A,KEY_A);game_update(0,0);
 for(int i=0;i<180;i++){u16 keys=i<60?KEY_RIGHT:i<90?KEY_DOWN:0;game_update(keys,0);if(!(i%6))game_update(KEY_A,KEY_A);}
 game_render();
 if(argc>1){char p[512];snprintf(p,sizeof(p),"%s-play.ppm",argv[1]);gfx_dump_ppm(p);}
 u32 seed=0xaced4321u;u16 old=0;
 for(int i=0;i<30000;i++){
  u16 held=(u16)(rng_next(&seed)&1023);
  /* Leave menus and offer long movement/attack runs between burst inputs. */
  if(i%100<80)held=(u16)(1u<<((i/100)%10));
  game_update(held,held&~old);old=held;
  if(!(i%100))game_render();
 }
 game_init();game_update(KEY_A,KEY_A);game_update(0,0);game_render();
 puts("PASS: rule regressions, 30,000 simulated input frames, renderer bounds (ASan/UBSan)");
 return 0;
}
