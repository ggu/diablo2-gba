/* Host renderer replay for exact pixel comparison with emulator captures. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/gba.h"

int main(int argc,char **argv) {
    FILE *in;
    char out[512];
    unsigned long total,tick,event_tick;
    unsigned event_held;
    unsigned long next_capture=0;
    u16 held=0,previous=0;
    if(argc!=4){fprintf(stderr,"usage: %s EVENTS OUTPUT_DIR TOTAL_TICKS\n",argv[0]);return 2;}
    total=strtoul(argv[3],NULL,10);
    in=fopen(argv[1],"r");if(!in){perror(argv[1]);return 2;}
    if(fscanf(in,"%lu %u",&event_tick,&event_held)!=2){event_tick=~0ul;}
    gba_init();game_init();
    for(tick=0;tick<=total;tick++) {
        if(tick==next_capture) {
            game_render();
            snprintf(out,sizeof(out),"%s/host-tick-%lu.ppm",argv[2],tick);
            gfx_dump_ppm(out);
            next_capture+=1000;
        }
        if(tick==total)break;
        while(event_tick==tick) {
            held=(u16)event_held;
            if(fscanf(in,"%lu %u",&event_tick,&event_held)!=2)event_tick=~0ul;
        }
        game_update(held,(u16)(held&~previous));previous=held;
        /* Exercise renderer caches and camera alignment on every simulation tick. */
        game_render();
    }
    if(total%1000!=0) {
        game_render();snprintf(out,sizeof(out),"%s/host-tick-%lu.ppm",argv[2],total);gfx_dump_ppm(out);
    }
    fclose(in);return 0;
}
