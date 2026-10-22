#ifndef GBA_H
#define GBA_H
#include <stdint.h>
#include <stddef.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
#if defined(GBA_HOST)
#define GBA_HOT
#else
#define GBA_HOT __attribute__((section(".iwram"),long_call,target("arm")))
#endif
enum { KEY_A=1,KEY_B=2,KEY_SELECT=4,KEY_START=8,KEY_RIGHT=16,KEY_LEFT=32,KEY_UP=64,KEY_DOWN=128,KEY_R=256,KEY_L=512 };
enum { C_BLACK=0,C_WHITE,C_DIM,C_PANEL,C_EDGE,C_RED,C_RED_DARK,C_GREEN,C_GREEN_DARK,C_BLUE,C_BLUE_DARK,C_GOLD,C_GOLD_DARK,C_CYAN,C_PURPLE,C_ORANGE,C_BROWN,C_DIRT,C_STONE,C_GRASS,C_SAND,C_SILVER,C_SKIN,C_SHADOW,C_TERRAN,C_ZERG,C_PROTOSS,C_LAVA,C_BLOOD,C_FOG,C_UI,C_HIGHLIGHT };
void gba_init(void);
u16 gba_keys(void);
u32 gba_clock(void);
void gfx_begin(u8 background);
void gfx_end(void);
int gfx_read_band(int y,int height,u8 *aligned_pixels);
int gfx_blit_band(int y,int height,const u8 *aligned_pixels);
int gfx_blit_window(int y,int height,const u8 *source,int stride,int rows,int left,int top);
#if defined(D2_DEN_OF_EVIL) || defined(GBA_HOST)
int gfx_blit_window_2x(int y,int height,const u8 *source,int stride,int rows,int left,int top);
#endif
void gfx_set_palette(int first,int count,const u16 *rgb555);
void gfx_blit16(int x,int y,const u8 *indexed_pixels);
void gfx_blit_sprite(int x,int y,int width,int height,const u8 *indexed_pixels);
#if defined(D2_DEN_OF_EVIL) || defined(GBA_HOST)
void gfx_blit_sprite_2x(int x,int y,int width,int height,const u8 *indexed_pixels);
#endif
void gfx_pixel(int x,int y,u8 color);
void gfx_rect(int x,int y,int w,int h,u8 color);
void gfx_frame(int x,int y,int w,int h,u8 color);
void gfx_line(int x0,int y0,int x1,int y1,u8 color);
void gfx_circle(int x,int y,int radius,u8 color);
void gfx_diamond(int x,int y,int rx,int ry,u8 color);
void gfx_text(int x,int y,const char* text,u8 color);
void gfx_num(int x,int y,int value,u8 color);
void gfx_bar(int x,int y,int w,int h,int value,int maximum,u8 color,u8 empty);
u32 rng_next(u32 *seed);
int clampi(int x,int lo,int hi);
int absi(int x);
void save_write(const void *data,u32 length,u32 tag);
int save_read(void *data,u32 length,u32 tag);
void audio_tone(int frequency,int frames,int volume);
void audio_update(void);
void game_init(void);
void game_update(u16 held,u16 pressed);
void game_render(void);
int game_self_test(void);
#ifdef GBA_HOST
void gfx_dump_ppm(const char *path);
void save_test_corrupt_latest(u32 length,u32 tag);
u8 gfx_test_pixel(int x,int y);
#endif
#endif
