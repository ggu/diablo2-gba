#include "gba.h"
volatile u32 demo_frame;
volatile u16 demo_inject_keys;
volatile u16 demo_keys;
volatile u32 demo_last_cycles, demo_peak_cycles;
volatile u32 demo_update_cycles;
volatile uint64_t demo_total_cycles;
volatile u32 demo_tick_buckets[8];
/* Optional debugger-fed controller events. Disabled in ordinary play. This
 * avoids a debugger round trip every tick during full-mission ARM replays. */
#if defined(D2_DEN_OF_EVIL)
#define DEMO_SCHEDULE_CAPACITY 2048
#else
#define DEMO_SCHEDULE_CAPACITY 128
#endif
volatile u32 demo_schedule_events[DEMO_SCHEDULE_CAPACITY][2];
volatile u32 demo_schedule_count, demo_schedule_length, demo_schedule_tick;
__attribute__((noinline)) void demo_replay_checkpoint(void) {
#ifdef D2_DEN_OF_EVIL
    extern void den_debug_checkpoint(void);
    den_debug_checkpoint();
#endif
    __asm__ volatile ("" ::: "memory");
}
int main(void) {
    gba_init();
    game_init();
    u16 previous=0;
    u32 schedule_index=0;
    u16 schedule_keys=0;
    for (;;) {
        u32 schedule_count=demo_schedule_count;
        if(schedule_count && !demo_schedule_tick && !schedule_index)
            demo_replay_checkpoint();
        u32 start=gba_clock();
        u16 held=gba_keys() | demo_inject_keys;
        if(schedule_count) {
            while(schedule_index<schedule_count && schedule_index<DEMO_SCHEDULE_CAPACITY &&
                  demo_schedule_events[schedule_index][0]==demo_schedule_tick) {
                schedule_keys=(u16)demo_schedule_events[schedule_index][1];
                schedule_index++;
            }
            held=schedule_keys;
        }
        demo_keys=held;
        game_update(held, held & (u16)~previous);
        demo_update_cycles=gba_clock()-start;
        audio_update();
        game_render();
        demo_last_cycles=gba_clock()-start;
        demo_total_cycles+=demo_last_cycles;
        u32 bucket=(demo_last_cycles+140448u)/280896u;
        demo_tick_buckets[bucket<7?bucket:7]++;
        if(demo_last_cycles>demo_peak_cycles)demo_peak_cycles=demo_last_cycles;
        previous=held;
        demo_frame++;
        if(schedule_count) {
            demo_schedule_tick++;
            if(demo_schedule_tick%1000u==0 || demo_schedule_tick==demo_schedule_length)
                demo_replay_checkpoint();
        }
    }
}
