#ifndef DEN_DISPLAY_H
#define DEN_DISPLAY_H

#include "../common/gba.h"

/* Display compensation for the original GBA/SP LCD. Lift shadows and midtones
   without changing the private source palette, transparent sprite indices,
   or the other games' palettes. This runs once, outside the frame loop. */
static void den_display_palette(const u16 *source) {
    static const u8 lcd[32]={
        2,6,8,10,11,12,14,15,16,17,18,18,19,20,21,22,
        22,23,24,24,25,26,26,27,27,28,28,29,29,30,30,31
    };
    u16 palette[224];
    for(int i=0;i<224;i++) {
        u16 color=source[i];
        palette[i]=(u16)(lcd[color&31]|(lcd[(color>>5)&31]<<5)|(lcd[(color>>10)&31]<<10));
    }
    gfx_set_palette(32,224,palette);

    /* Keep the near-black panel background; brighten text, borders and the
       authored town silhouettes independently of the original-art palette. */
    static const struct {u8 index,r,g,b;} ui[]={
        {C_DIM,23,25,27},{C_PANEL,4,7,10},{C_EDGE,14,19,23},
        {C_RED_DARK,18,4,5},{C_BLUE_DARK,4,9,22},
        {C_GOLD_DARK,23,18,7},{C_BROWN,20,14,8},{C_DIRT,17,14,9},
        {C_STONE,20,21,21},{C_GRASS,11,18,11}
    };
    for(unsigned i=0;i<sizeof(ui)/sizeof(ui[0]);i++) {
        u16 color=(u16)(ui[i].r|(ui[i].g<<5)|(ui[i].b<<10));
        gfx_set_palette(ui[i].index,1,&color);
    }
}

#endif
