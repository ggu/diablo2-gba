/* Save migration and rejection edge cases; no renderer or ROM required. */
#include <assert.h>
#include <string.h>
#include "../diablo2/den_sim.h"

int main(void)
{
    DenSave bad,legacy;
    DenState unchanged;
    unsigned old_size=8+offsetof(DenState,gear);
    int points;

    /* A valid v1 completed quest loads as already rewarded and in town. */
    den_start_new();d.area=DEN_AREA_CAVE;d.px=DEN_ENTRY_X*8+4;d.py=DEN_ENTRY_Y*8+4;
    d.quest=DEN_QUEST_DONE;d.skill_points=3;
    legacy=(DenSave){0};legacy.magic=0x44454E53u;legacy.version=1;
    legacy.size=(u16)old_size;legacy.state=d;
    save_write(&legacy,old_size,DEN_SAVE_TAG);
    den_init();assert(den_load());
    assert(d.quest==DEN_QUEST_DONE&&d.rewarded&&d.area==DEN_AREA_TOWN&&d.screen==DEN_PLAY);
    assert(d.skill_points==3);
    points=d.skill_points;den_talk_akara();assert(d.dialog==DEN_DIALOG_THANKS);
    den_dialog_input(KEY_A);assert(d.skill_points==points&&d.quest==DEN_QUEST_DONE);

    den_start_new();
    d.gold=123;den_save();
    d.gold=777;unchanged=d;
    bad=(DenSave){0};bad.magic=0x44454E53u;bad.version=DEN_SAVE_VERSION;
    bad.size=(u16)sizeof(bad);bad.state=unchanged;
    bad.state.gear.grid[0]=255; /* structurally invalid, but save_write gives it a valid checksum */
    save_write(&bad,sizeof(bad),DEN_SAVE_TAG);
    assert(!den_load());
    assert(d.gold==unchanged.gold&&d.tick==unchanged.tick&&d.area==unchanged.area);
    return 0;
}
