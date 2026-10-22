#include "gba.h"
#ifdef GBA_HOST
#include <stdio.h>
static u16 host_fb[2][19200];
static u8 host_save[32768];
static u16 host_palette[256];
#endif
#define REG16(a) (*(volatile u16 *)(a))
#define REG32(a) (*(volatile u32 *)(a))
#ifdef GBA_HOST
#define HOT
#else
#define HOT __attribute__((section(".iwram"),long_call,target("arm")))
#endif
#define RGB(r,g,b) ((r)|((g)<<5)|((b)<<10))
/* Direct GBA mode-4 double buffering. All framebuffer stores are halfwords. */
#ifdef GBA_HOST
static volatile u16 *pixels=host_fb[1];
#else
static volatile u16 *pixels=(volatile u16*)0x0600a000;
#endif
static u16 front;
static int tone_frames;
volatile u32 gfx_draw_cycles;
static u32 draw_start;
static u32 previous_present;
static const u16 colors[32]={
 RGB(1,1,2),RGB(30,30,28),RGB(13,15,17),RGB(3,5,8),RGB(8,12,15),
 RGB(30,5,5),RGB(12,2,3),RGB(7,28,12),RGB(2,10,5),RGB(6,14,30),RGB(2,5,15),
 RGB(31,25,9),RGB(14,10,3),RGB(9,28,29),RGB(22,10,28),RGB(31,15,4),
 RGB(14,9,5),RGB(8,7,5),RGB(12,13,13),RGB(5,10,6),RGB(20,18,11),RGB(21,23,24),
 RGB(27,19,13),RGB(2,3,3),RGB(9,17,26),RGB(19,8,17),RGB(29,23,12),RGB(28,8,1),
 RGB(12,1,2),RGB(2,3,5),RGB(8,11,12),RGB(31,31,18)
};
/* Cartridge backup signature used by emulator/flash-cart save-type detection. */
__attribute__((used)) const char gba_save_signature[]="SRAM_V113";
void gba_init(void) {
#ifdef GBA_HOST
 front=0; pixels=host_fb[1];
 for(int i=0;i<32;i++)host_palette[i]=colors[i];
#else
 REG16(0x04000208)=0;
 REG16(0x04000000)=0x0404;
 REG16(0x04000204)=0x4317;
 volatile u16 *pal=(volatile u16*)0x05000000;
 for(int i=0;i<32;i++)pal[i]=colors[i];
 REG16(0x04000084)=0x0080;
 REG16(0x04000080)=0x1177;
 REG16(0x04000082)=0x0002;
 REG16(0x04000108)=0; REG16(0x0400010c)=0;
 REG16(0x0400010e)=0x0084; REG16(0x0400010a)=0x0080;
#endif
}
void gfx_set_palette(int first,int count,const u16 *rgb555) {
 if(first<0||count<0||first+count>256||!rgb555)return;
#ifdef GBA_HOST
 for(int i=0;i<count;i++)host_palette[first+i]=rgb555[i];
#else
 volatile u16 *pal=(volatile u16*)0x05000000;
 for(int i=0;i<count;i++)pal[first+i]=rgb555[i];
#endif
}
u32 gba_clock(void){
#ifdef GBA_HOST
return 0;
#else
u16 hi,lo;do{hi=REG16(0x0400010c);lo=REG16(0x04000108);}while(hi!=REG16(0x0400010c));return ((u32)hi<<16)|lo;
#endif
}
u16 gba_keys(void){
#ifdef GBA_HOST
return 0;
#else
return (~REG16(0x04000130))&1023;
#endif
}
#ifndef GBA_HOST
static void vblank(void){while(REG16(0x04000006)>=160){}while(REG16(0x04000006)<160){}}
#endif
void gfx_begin(u8 color) {
 draw_start=gba_clock();
#ifdef GBA_HOST
 pixels=host_fb[front^1];
 for(int i=0;i<19200;i++)pixels[i]=color|((u16)color<<8);
#else
 pixels=(volatile u16*)(front?0x06000000:0x0600a000);
 volatile u32 fill=color|((u32)color<<8)|((u32)color<<16)|((u32)color<<24);
 REG32(0x040000d4)=(u32)&fill;
 REG32(0x040000d8)=(u32)pixels;
 /* A 32-bit transfer clears four Mode 4 pixels per DMA beat. */
 REG32(0x040000dc)=0x85000000|9600;
 while(REG32(0x040000dc)&0x80000000){}
#endif
}
/* Copy an exact full-width band between Mode 4 pixels and aligned linear
   indexed-pixel storage. The GBA DMA path uses whole 32-bit words only. */
static int gfx_band_valid(int y,int height,const void *buffer) {
 return buffer&&(((uintptr_t)buffer&3)==0)&&y>=0&&height>0&&height<=160&&y<=160-height;
}
HOT int gfx_read_band(int y,int height,u8 *dst) {
 if(!gfx_band_valid(y,height,dst))return 0;
#ifdef GBA_HOST
 for(int row=0;row<height;row++) {
  const volatile u16 *src=pixels+(y+row)*120;
  u8 *out=dst+row*240;
  for(int x=0;x<120;x++) {
   u16 pair=src[x];out[x*2]=(u8)pair;out[x*2+1]=(u8)(pair>>8);
  }
 }
#else
 REG32(0x040000d4)=(u32)(pixels+y*120);
 REG32(0x040000d8)=(u32)dst;
 REG32(0x040000dc)=0x84000000|(240*height/4);
 while(REG32(0x040000dc)&0x80000000){}
#endif
 return 1;
}
HOT int gfx_blit_band(int y,int height,const u8 *src) {
 if(!gfx_band_valid(y,height,src))return 0;
#ifdef GBA_HOST
 for(int row=0;row<height;row++) {
  volatile u16 *out=pixels+(y+row)*120;
  const u8 *in=src+row*240;
  for(int x=0;x<120;x++)out[x]=in[x*2]|((u16)in[x*2+1]<<8);
 }
#else
 REG32(0x040000d4)=(u32)src;
 REG32(0x040000d8)=(u32)(pixels+y*120);
 REG32(0x040000dc)=0x84000000|(240*height/4);
 while(REG32(0x040000dc)&0x80000000){}
#endif
 return 1;
}
/* Opaque clipped 240-pixel window from aligned, padded source rows. Areas
   outside the source keep the framebuffer's current background. The shifted
   word path preserves every camera pixel without unaligned ARM7 loads. */
HOT int gfx_blit_window(int y,int height,const u8 *source,int stride,int rows,int left,int top) {
 if(!source||((uintptr_t)source&3)||stride<=0||(stride&3)||rows<=0||
    y<0||height<=0||height>160||y>160-height)return 0;
 for(int row=0;row<height;row++) {
  int sy=top+row;if(sy<0||sy>=rows)continue;
  int begin=left<0?-left:0,end=left+240>stride?stride-left:240;
  if(begin>=end||begin>=240||end<=0)continue;
  int col=begin;
  const u8 *in=source+sy*stride+left+col;
  volatile u16 *out=pixels+(y+row)*120+(col>>1);
  if(col&1){*out=(*out&255)|((u16)*in++<<8);out++;col++;}
  if(col+2<=end&&((uintptr_t)out&2)){
   *out++=in[0]|((u16)in[1]<<8);in+=2;col+=2;
  }
  int words=(end-col)/4;
  volatile u32 *wide=(volatile u32*)out;
  if(words) {
   const u32 *aligned=(const u32*)((uintptr_t)in&~(uintptr_t)3);
   int shift=((uintptr_t)in&3)*8;
   if(!shift) {
#ifdef GBA_HOST
    for(int n=0;n<words;n++)wide[n]=aligned[n];
#else
    /* Opaque, aligned source rows can move directly from EWRAM to VRAM.
       The edge pairs and byte-shifted rows retain their exact CPU path. */
    REG32(0x040000d4)=(u32)aligned;
    REG32(0x040000d8)=(u32)wide;
    REG32(0x040000dc)=0x84000000|(u32)words;
    while(REG32(0x040000dc)&0x80000000){}
#endif
   } else {
    u32 a=*aligned++;
    for(int n=0;n<words;n++){u32 b=*aligned++;wide[n]=(a>>shift)|(b<<(32-shift));a=b;}
   }
   in+=words*4;col+=words*4;out=(volatile u16*)(wide+words);
  }
  if(col+2<=end){*out++=in[0]|((u16)in[1]<<8);in+=2;col+=2;}
  if(col<end)*out=(*out&0xff00)|*in;
 }
 return 1;
}
#if defined(D2_DEN_OF_EVIL) || defined(GBA_HOST)
/* Nearest-neighbour 2x opaque cache window. Each source pixel becomes a
   2x2 block; the caller's framebuffer fill supplies pixels beyond cache edges. */
HOT int gfx_blit_window_2x(int y,int height,const u8 *source,int stride,int rows,int left,int top) {
 if(!source||((uintptr_t)source&3)||stride<=0||(stride&3)||rows<=0||
    y<0||height<=0||(height&1)||height>160||y>160-height)return 0;
 int out_rows=height>>1;
 for(int row=0;row<out_rows;row++) {
  int sy=top+row;if(sy<0||sy>=rows)continue;
  int begin=left<0?-left:0,end=left+120>stride?stride-left:120;
  if(begin>=end||begin>=120||end<=0)continue;
  const u8 *in=source+sy*stride+left+begin;
  int col=begin;
  volatile u16 *out=pixels+(y+row*2)*120+col;
  volatile u16 *out2=out+120;
  for(;col<end;col++,in++,out++,out2++) {
   u16 doubled=*in|((u16)*in<<8);
   *out=doubled;*out2=doubled;
  }
 }
 return 1;
}
#endif
void gfx_end(void) {
    gfx_draw_cycles=gba_clock()-draw_start;
#ifndef GBA_HOST
    /* Target 30 updates/s without adding a third video frame when drawing
       already consumed more than one 59.73 Hz hardware frame. */
    u32 elapsed=gba_clock()-previous_present;
    vblank();
#endif
    front^=1;
#ifndef GBA_HOST
    REG16(0x04000000)=0x0404|(front?16:0);
    if(elapsed<280896u)vblank();
    previous_present=gba_clock();
#endif
}
HOT void gfx_pixel(int x,int y,u8 c){if((unsigned)x>=240||(unsigned)y>=160)return;volatile u16*p=pixels+y*120+(x>>1);u16 old=*p;*p=(x&1)?((old&255)|((u16)c<<8)):((old&0xff00)|c);}
HOT void gfx_blit16(int x,int y,const u8 *src) {
 if(!src||x>=240||y>=160||x<=-16||y<=-16)return;
 int top=y<0?-y:0,bottom=y+16>160?160-y:16;
 if(x>=0&&x<=224&&!(x&3)&&!((uintptr_t)src&3)) {
  for(int row=top;row<bottom;row++) {
   const u32 *s=(const u32*)(src+row*16);
   volatile u32 *d=(volatile u32*)(pixels+(y+row)*120+x/2);
   d[0]=s[0];d[1]=s[1];d[2]=s[2];d[3]=s[3];
  }
 } else if(x>=0&&x<=224&&(x&3)==2&&!((uintptr_t)src&3)) {
  /* Camera offsets are commonly two pixels off a word boundary. Align the
     destination with a leading pixel pair, then join adjacent source words
     so the 12 middle pixels still use aligned 32-bit VRAM stores. */
  for(int row=top;row<bottom;row++) {
   const u32 *s=(const u32*)(src+row*16);
   u32 a=s[0],b=s[1],c=s[2],e=s[3];
   volatile u16 *d=pixels+(y+row)*120+x/2;
   *d++=(u16)a;
   volatile u32 *wide=(volatile u32*)d;
   wide[0]=(a>>16)|(b<<16);
   wide[1]=(b>>16)|(c<<16);
   wide[2]=(c>>16)|(e<<16);
   d=(volatile u16*)(wide+3);
   *d=(u16)(e>>16);
  }
 } else {
  int left=x<0?-x:0,right=x+16>240?240-x:16;
  for(int row=top;row<bottom;row++) {
   int col=left;
   volatile u16 *d=pixels+(y+row)*120+((x+col)>>1);
   if((x+col)&1){*d=(*d&255)|((u16)src[row*16+col]<<8);d++;col++;}
   for(;col+1<right;col+=2)*d++=src[row*16+col]|((u16)src[row*16+col+1]<<8);
   if(col<right)*d=(*d&0xff00)|src[row*16+col];
  }
 }
}
HOT void gfx_blit_sprite(int x,int y,int width,int height,const u8 *src) {
 if(!src||width<=0||height<=0||width>256||height>256||x>=240||y>=160||x+width<=0||y+height<=0)return;
 int top=y<0?-y:0,bottom=y+height>160?160-y:height;
 int left=x<0?-x:0,right=x+width>240?240-x:width;
 for(int row=top;row<bottom;row++) {
  int col=left;const u8*s=src+row*width;
  volatile u16*d=pixels+(y+row)*120+((x+col)>>1);
  if((x+col)&1){if(s[col])*d=(*d&255)|((u16)s[col]<<8);d++;col++;}
  for(;col+1<right;col+=2,d++){
   u8 a=s[col],b=s[col+1];
   if(a&&b)*d=a|((u16)b<<8);
   else if(a)*d=(*d&0xff00)|a;
   else if(b)*d=(*d&255)|((u16)b<<8);
  }
  if(col<right&&s[col])*d=(*d&0xff00)|s[col];
 }
}
#if defined(D2_DEN_OF_EVIL) || defined(GBA_HOST)
/* Transparent nearest-neighbour 2x sprite expansion for the Den scene. */
HOT void gfx_blit_sprite_2x(int x,int y,int width,int height,const u8 *src) {
 if(!src||width<=0||height<=0||width>256||height>256||x>=240||y>=160||x+width*2<=0||y+height*2<=0)return;
 int top=y<0?(-y)/2:0,bottom=(y+height*2>160)?(160-y+1)/2:height;
 int left=x<0?(-x)/2:0,right=(x+width*2>240)?(240-x+1)/2:width;
 if(bottom>height)bottom=height;
 if(right>width)right=width;
 for(int row=top;row<bottom;row++) {
  const u8 *s=src+row*width;
  for(int col=left;col<right;col++)if(s[col]) {
   u8 c=s[col];int dx=x+col*2,dy=y+row*2;
   for(int yy=0;yy<2;yy++)if((unsigned)(dy+yy)<160) {
    volatile u16 *d=pixels+(dy+yy)*120;
    if(dx>=0&&dx+1<240&&!(dx&1))d[dx>>1]=c|((u16)c<<8);
    else {if(dx>=0&&dx<240)gfx_pixel(dx,dy+yy,c);if(dx+1>=0&&dx+1<240)gfx_pixel(dx+1,dy+yy,c);}
   }
  }
 }
}
#endif
HOT void gfx_rect(int x,int y,int w,int h,u8 color) {
    if(w<=0||h<=0||x>=240||y>=160||x+w<=0||y+h<=0)return;
    if(x<0){w+=x;x=0;}if(y<0){h+=y;y=0;}
    if(x+w>240)w=240-x;
    if(y+h>160)h=160-y;
    const u16 pair=color|((u16)color<<8);
    const u32 quad=(u32)pair|((u32)pair<<16);
    for(int row=y;row<y+h;row++) {
        int left=w,xx=x;
        volatile u16 *dst=pixels+row*120+(xx>>1);
        if(xx&1){*dst=(*dst&255)|((u16)color<<8);dst++;left--;xx++;}
        if((xx&2)&&left>=2){*dst++=pair;left-=2;}
        volatile u32 *wide=(volatile u32*)dst;
        while(left>=16){wide[0]=quad;wide[1]=quad;wide[2]=quad;wide[3]=quad;wide+=4;left-=16;}
        while(left>=4){*wide++=quad;left-=4;}
        dst=(volatile u16*)wide;
        if(left>=2){*dst++=pair;left-=2;}
        if(left)*dst=(*dst&0xff00)|color;
    }
}
HOT void gfx_frame(int x,int y,int w,int h,u8 c){gfx_rect(x,y,w,1,c);gfx_rect(x,y+h-1,w,1,c);gfx_rect(x,y,1,h,c);gfx_rect(x+w-1,y,1,h,c);}
int absi(int x){return x<0?-x:x;}
int clampi(int x,int lo,int hi){return x<lo?lo:(x>hi?hi:x);}
HOT void gfx_line(int x0,int y0,int x1,int y1,u8 c){int dx=absi(x1-x0),sx=x0<x1?1:-1,dy=-absi(y1-y0),sy=y0<y1?1:-1,err=dx+dy;for(;;){gfx_pixel(x0,y0,c);if(x0==x1&&y0==y1)break;int e=2*err;if(e>=dy){err+=dy;x0+=sx;}if(e<=dx){err+=dx;y0+=sy;}}}
HOT void gfx_circle(int x,int y,int r,u8 c){if(r<0||r>240)return;for(int yy=-r;yy<=r;yy++)for(int xx=-r;xx<=r;xx++)if(xx*xx+yy*yy<=r*r)gfx_pixel(x+xx,y+yy,c);}
HOT void gfx_diamond(int x,int y,int rx,int ry,u8 color) {
    if(rx<0||ry<0||rx>480||ry>320)return;
    if(!ry){gfx_rect(x-rx,y,rx*2+1,1,color);return;}
    const u16 pair=color|((u16)color<<8);
    for(int yy=-ry;yy<=ry;yy++) {
        int span=ry-(yy<0?-yy:yy);
        int half=rx==2*ry?span*2:(rx==ry?span:rx*span/ry);
        int left=x-half,right=x+half,sy=y+yy;
        if((unsigned)sy>=160||right<0||left>=240)continue;
        if(left<0)left=0;
        if(right>239)right=239;
        volatile u16 *dst=pixels+sy*120+(left>>1);
        if(left&1){*dst=(*dst&255)|((u16)color<<8);dst++;left++;}
        for(;left+1<=right;left+=2)*dst++=pair;
        if(left<=right)*dst=(*dst&0xff00)|color;
    }
}
static const u8 font[96][7]={
 [33]={14,17,17,31,17,17,17},
 [34]={30,17,17,30,17,17,30},
 [35]={15,16,16,16,16,16,15},
 [36]={30,17,17,17,17,17,30},
 [37]={31,16,16,30,16,16,31},
 [38]={31,16,16,30,16,16,16},
 [39]={15,16,16,23,17,17,15},
 [40]={17,17,17,31,17,17,17},
 [41]={31,4,4,4,4,4,31},
 [42]={7,2,2,2,18,18,12},
 [43]={17,18,20,24,20,18,17},
 [44]={16,16,16,16,16,16,31},
 [45]={17,27,21,21,17,17,17},
 [46]={17,25,25,21,19,19,17},
 [47]={14,17,17,17,17,17,14},
 [48]={30,17,17,30,16,16,16},
 [49]={14,17,17,17,21,18,13},
 [50]={30,17,17,30,20,18,17},
 [51]={15,16,16,14,1,1,30},
 [52]={31,4,4,4,4,4,4},
 [53]={17,17,17,17,17,17,14},
 [54]={17,17,17,17,17,10,4},
 [55]={17,17,17,21,21,21,10},
 [56]={17,17,10,4,10,17,17},
 [57]={17,17,10,4,4,4,4},
 [58]={31,1,2,4,8,16,31},
 [16]={14,17,19,21,25,17,14},
 [17]={4,12,4,4,4,4,14},
 [18]={14,17,1,2,4,8,31},
 [19]={30,1,1,14,1,1,30},
 [20]={2,6,10,18,31,2,2},
 [21]={31,16,16,30,1,1,30},
 [22]={14,16,16,30,17,17,14},
 [23]={31,1,2,4,8,8,8},
 [24]={14,17,17,14,17,17,14},
 [25]={14,17,17,15,1,1,14},
 [26]={0,4,4,0,4,4,0},
 [15]={1,2,2,4,8,8,16},
 [13]={0,0,0,31,0,0,0},
 [11]={0,4,4,31,4,4,0},
 [14]={0,0,0,0,0,6,6},
 [12]={0,0,0,0,6,6,4},
 [1]={4,4,4,4,4,0,4},
 [31]={14,17,1,2,4,0,4},
 [29]={0,0,31,0,31,0,0},
 [59]={14,8,8,8,8,8,14},
 [61]={14,2,2,2,2,2,14},
 [8]={2,4,8,8,8,4,2},
 [9]={8,4,2,2,2,4,8},
 [5]={25,26,2,4,8,11,19},
 [30]={16,8,4,2,4,8,16},
 [28]={1,2,4,8,4,2,1},
 [10]={0,21,14,31,14,21,0},
 [63]={0,0,0,0,0,0,31},
 [7]={4,4,0,0,0,0,0},
};

static HOT void glyph_row(int x,int y,u8 bits,u8 color) {
    if ((unsigned)y>=160) return;
    int col=0;
    /* Start on a halfword boundary, then update pairs with a single store. */
    if (x%2) {
        int px=x;
        if ((unsigned)px<240 && (bits&16)) {
            volatile u16 *dst=pixels+y*120+(px>>1);
            *dst=(*dst&255)|((u16)color<<8);
        }
        col=1;
    }
    for (;col+1<5;col+=2) {
        int px=x+col;
        if ((unsigned)px>=240 || px+1>=240) continue;
        int first=(bits&(16>>col))!=0;
        int second=(bits&(16>>(col+1)))!=0;
        if (!first&&!second) continue;
        volatile u16 *dst=pixels+y*120+(px>>1);
        if (first&&second) *dst=color|((u16)color<<8);
        else if (first) *dst=(*dst&0xff00)|color;
        else *dst=(*dst&255)|((u16)color<<8);
    }
    if (col<5) {
        int px=x+col;
        if ((unsigned)px<240 && (bits&(16>>col))) {
            volatile u16 *dst=pixels+y*120+(px>>1);
            *dst=(*dst&0xff00)|color;
        }
    }
}
HOT void gfx_text(int x,int y,const char*t,u8 c){int origin=x;for(;*t;t++){u8 ch=(u8)*t;if(ch=='\n'){x=origin;y+=9;continue;}if(ch>='a'&&ch<='z')ch-=32;if(ch>=32&&ch<128&&x>-6&&x<240)for(int row=0;row<7;row++)glyph_row(x,y+row,font[ch-32][row],c);x+=6;}}
HOT void gfx_num(int x,int y,int n,u8 c){char b[12];int i=0;u32 v;if(n<0){gfx_text(x,y,"-",c);x+=6;v=0u-(u32)n;}else v=(u32)n;do{b[i++]='0'+v%10;v/=10;}while(v&&i<11);for(int j=0;j<i;j++){char z[2]={b[i-j-1],0};gfx_text(x+j*6,y,z,c);}}
HOT void gfx_bar(int x,int y,int w,int h,int v,int max,u8 c,u8 empty){gfx_rect(x,y,w,h,empty);if(max>0)gfx_rect(x,y,w*clampi(v,0,max)/max,h,c);}
u32 rng_next(u32*s){u32 x=*s;if(!x)x=0x6d2b79f5u;x^=x<<13;x^=x>>17;x^=x<<5;*s=x;return x;}
static u32 hash_bytes(const u8*p,u32 n){u32 h=2166136261u;while(n--){h^=*p++;h*=16777619u;}return h;}
#ifdef GBA_HOST
#define SRAM_BASE host_save
#else
#define SRAM_BASE ((u8*)0x0e000000)
#endif
static void sram_put32(volatile u8*p,u32 v){for(int i=0;i<4;i++)p[i]=(u8)(v>>(8*i));}
static u32 sram_get32(volatile const u8*p){u32 v=0;for(int i=0;i<4;i++)v|=(u32)p[i]<<(8*i);return v;}
/* Two alternating 16 KiB records preserve the previous save if power is
   lost during a write. Commit the tag last; a checksum covers the payload. */
#define SAVE_SLOT_SIZE 16384u
#define SAVE_PAYLOAD_MAX (SAVE_SLOT_SIZE-16u)
static int valid_record(volatile const u8 *p,u32 len,u32 tag) {
    if(sram_get32(p)!=tag||sram_get32(p+4)!=len||!sram_get32(p+12))return 0;
    u32 h=2166136261u;
    for(u32 i=0;i<len;i++){h^=p[16+i];h*=16777619u;}
    return h==sram_get32(p+8);
}
static int newest_record(u32 len,u32 tag) {
    volatile const u8 *base=(volatile const u8*)SRAM_BASE;
    int a=valid_record(base,len,tag),b=valid_record(base+SAVE_SLOT_SIZE,len,tag);
    if(!a&&!b)return -1;
    if(!a)return 1;
    if(!b)return 0;
    return (s32)(sram_get32(base+SAVE_SLOT_SIZE+12)-sram_get32(base+12))>0?1:0;
}
void save_write(const void *data,u32 len,u32 tag) {
    if(len>SAVE_PAYLOAD_MAX||!tag)return;
    volatile u8 *base=(volatile u8*)SRAM_BASE;
    int newest=newest_record(len,tag);
    u32 sequence=newest<0?1:sram_get32(base+newest*SAVE_SLOT_SIZE+12)+1;
    if(!sequence)sequence=1;
    volatile u8 *p=base+(newest==0?SAVE_SLOT_SIZE:0);
    const u8 *src=data;
    sram_put32(p,0);
    sram_put32(p+4,len);
    sram_put32(p+8,hash_bytes(src,len));
    sram_put32(p+12,sequence);
    for(u32 i=0;i<len;i++)p[16+i]=src[i];
    sram_put32(p,tag);
}
int save_read(void *data,u32 len,u32 tag) {
    if(len>SAVE_PAYLOAD_MAX||!tag)return 0;
    int newest=newest_record(len,tag);
    if(newest<0)return 0;
    volatile const u8 *src=(volatile const u8*)SRAM_BASE+newest*SAVE_SLOT_SIZE+16;
    u8 *dst=data;
    for(u32 i=0;i<len;i++)dst[i]=src[i];
    return 1;
}
#ifdef GBA_HOST
void save_test_corrupt_latest(u32 len,u32 tag) {
    int newest=newest_record(len,tag);
    if(newest>=0)host_save[newest*SAVE_SLOT_SIZE+16]^=0x80;
}
#endif
void audio_tone(int hz,int frames,int volume){
#ifdef GBA_HOST
(void)hz; (void)frames; (void)volume;
#else
hz=clampi(hz,64,2048);int f=2048-131072/hz;REG16(0x04000068)=((clampi(volume,0,15)<<12)|0x0080);REG16(0x0400006c)=0x8000|f;tone_frames=frames;
#endif
}
void audio_update(void){
#ifndef GBA_HOST
if(tone_frames>0&&!--tone_frames)REG16(0x04000068)=0;
#endif
}

#ifdef GBA_HOST
u8 gfx_test_pixel(int x,int y){if((unsigned)x>=240||(unsigned)y>=160)return 0;u16 v=pixels[y*120+x/2];return (u8)(x&1?v>>8:v);}
void gfx_dump_ppm(const char* path){FILE*f=fopen(path,"wb");if(!f)return;fprintf(f,"P6\n240 160\n255\n");for(int i=0;i<38400;i++){u16 a=host_fb[front][i/2];u8 idx=(i&1)?a>>8:a&255;u16 v=host_palette[idx];u8 rgb[3]={(u8)((v&31)*255/31),(u8)(((v>>5)&31)*255/31),(u8)(((v>>10)&31)*255/31)};fwrite(rgb,1,3,f);}fclose(f);}
#endif
