/* Controller-only completion replay for the optional Diablo II Den slice.
 * State reads select a route and verify checkpoints; gameplay changes are
 * delivered only as controller events through den_update(). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../diablo2/den_sim.h"

typedef struct { unsigned long tick; u16 held; } DenInputEvent;
typedef struct { unsigned long tick; u32 hash; int screen,hp,quest,kills,px,py,area,rewarded,skill_points,weapon,shield,torso,items; } DenCheckpoint;
static DenInputEvent events[2048];
static DenCheckpoint checks[128];
static int event_count,check_count,gameplay_frames;
static int event_overflow;
static unsigned long input_tick;
static u16 previous;

static void record_checkpoint(void)
{
    DenCheckpoint *c;
    if(check_count>=(int)(sizeof(checks)/sizeof(checks[0])))return;
    c=&checks[check_count++];c->tick=input_tick;c->hash=den_state_hash();c->screen=d.screen;
    c->hp=d.hp;c->quest=d.quest;c->kills=d.kills;c->px=d.px;c->py=d.py;
    c->area=d.area;c->rewarded=d.rewarded;c->skill_points=d.skill_points;
    c->weapon=d.gear.equipment[DEN_SLOT_WEAPON]?d.gear.items[d.gear.equipment[DEN_SLOT_WEAPON]-1].base_id:0;
    c->shield=d.gear.equipment[DEN_SLOT_SHIELD]?d.gear.items[d.gear.equipment[DEN_SLOT_SHIELD]-1].base_id:0;
    c->torso=d.gear.equipment[DEN_SLOT_TORSO]?d.gear.items[d.gear.equipment[DEN_SLOT_TORSO]-1].base_id:0;
    {int i;c->items=0;for(i=0;i<DEN_ITEM_RECORDS;i++)c->items+=!!d.gear.items[i].base_id;}
}
static void input_frame(u16 held)
{
    u16 pressed=held&(u16)~previous;
    if(held!=previous) {
        if(event_count>=(int)(sizeof(events)/sizeof(events[0])))event_overflow=1;
        else {events[event_count].tick=input_tick;events[event_count++].held=held;}
    }
    if(d.screen==DEN_PLAY)++gameplay_frames;
    den_update(held,pressed);previous=held;++input_tick;
    if(input_tick%1000==0)record_checkpoint();
}
static void press(u16 key){input_frame(key);input_frame(0);}
static int can_move_tile(int x,int y){return den_floor(x,y);}

/* Four-neighbor BFS reads the authored floor and emits ordinary D-pad input. */
static int move_to_tile(int gx,int gy)
{
    static s16 qx[DEN_MAP_W*DEN_MAP_H],qy[DEN_MAP_W*DEN_MAP_H];
    static s8 prevdir[DEN_MAP_H][DEN_MAP_W];
    static const s8 dx[4]={1,-1,0,0},dy[4]={0,0,1,-1};
    int sx=den_tx(d.px),sy=den_ty(d.py),head=0,tail=0,k,found=0,x,y;
    if(sx==gx&&sy==gy)return 1;
    memset(prevdir,-2,sizeof(prevdir));
    qx[tail]=(s16)sx;qy[tail++]=(s16)sy;prevdir[sy][sx]=-1;
    while(head<tail&&!found) {
        x=qx[head];y=qy[head++];
        for(k=0;k<4;k++){int nx=x+dx[k],ny=y+dy[k];
            if(can_move_tile(nx,ny)&&prevdir[ny][nx]==-2){prevdir[ny][nx]=(s8)k;qx[tail]=(s16)nx;qy[tail++]=(s16)ny;if(nx==gx&&ny==gy){found=1;break;}}
        }
    }
    if(!found)return 0;
    x=gx;y=gy;
    while(1){int dir=prevdir[y][x],nx=x-dx[dir],ny=y-dy[dir];if(nx==sx&&ny==sy)break;x=nx;y=ny;}
    if(x>sx)k=0;else if(x<sx)k=1;else if(y>sy)k=2;else k=3;
    {
        u16 dir=k==0?(KEY_RIGHT|KEY_DOWN):k==1?(KEY_LEFT|KEY_UP):k==2?(KEY_DOWN|KEY_LEFT):(KEY_UP|KEY_RIGHT);
        for(k=0;k<8;k++)input_frame(dir);
    }
    input_frame(0);
    return 1;
}
static int nearest_alive(void)
{
    int i,b=-1,best=0x7fff;
    for(i=0;i<d.mob_count;i++)if(d.mobs[i].alive){
        int dist=den_distance(d.px,d.py,d.mobs[i].x,d.mobs[i].y);
        if(dist<best){best=dist;b=i;}
    }
    return b;
}
static int route_to_target(int idx)
{
    int guard=0;
    while(d.mobs[idx].alive&&guard++<4000) {
        int tx=den_tx(d.mobs[idx].x),ty=den_ty(d.mobs[idx].y);
        int dist=den_distance(d.px,d.py,d.mobs[idx].x,d.mobs[idx].y);
        if(d.hp<=28&&d.potions){press(KEY_R);continue;}
        if(dist<=DEN_BASH_RANGE){
            if(d.mobs[idx].elite && d.mana>=2)press(KEY_B);
            else input_frame(KEY_A);
            continue;
        }
        if(tx==den_tx(d.px)&&ty==den_ty(d.py)) {
            int dx=d.mobs[idx].x-d.px,dy=d.mobs[idx].y-d.py;
            u16 dir=den_abs(dx)>=den_abs(dy)?(dx>0?(KEY_RIGHT|KEY_DOWN):(KEY_LEFT|KEY_UP)):(dy>0?(KEY_DOWN|KEY_LEFT):(KEY_UP|KEY_RIGHT));
            input_frame(dir);continue;
        }
        if(!move_to_tile(tx,ty))return 0;
    }
    return !d.mobs[idx].alive;
}
static int walk_to_tile(int x,int y,int limit)
{
    int steps=0;
    while((den_tx(d.px)!=x||den_ty(d.py)!=y)&&steps++<limit)
        if(!move_to_tile(x,y))return 0;
    return den_tx(d.px)==x&&den_ty(d.py)==y;
}
static int walk_to_akara(void)
{ return walk_to_tile(DEN_AKARA_X,DEN_AKARA_Y,300); }
static int pickup_and_equip_torso(void)
{
    int i,loot=-1,guard=0,item_index=-1;
    for(i=0;i<DEN_LOOT_MAX;i++)if(d.loot[i].active&&d.loot[i].kind==DEN_LOOT_ITEM){loot=i;break;}
    if(loot<0)return 0;
    if(!walk_to_tile(den_tx(d.loot[loot].x),den_ty(d.loot[loot].y),300))return 0;
    while(d.loot[loot].active&&guard++<DEN_LOOT_MAX){press(KEY_A);}
    if(d.loot[loot].active)return 0;
    for(i=0;i<DEN_ITEM_RECORDS;i++)if(d.gear.items[i].base_id==DEN_ITEM_QUILTED_ARMOR&&
        d.gear.equipment[DEN_SLOT_TORSO]!=(u8)(i+1)){item_index=i;break;}
    if(item_index<0)return 0;
    /* Items enter at the first free grid cell; the boss's armor goes at cell 0. */
    press(KEY_L);
    if(d.inventory!=1||d.gear.grid[d.inv_cursor]!=(u8)(item_index+1))return 0;
    press(KEY_A);
    if(d.gear.equipment[DEN_SLOT_TORSO]!=(u8)(item_index+1))return 0;
    press(KEY_L);
    return d.inventory==0;
}
static void write_trace(const char *path)
{
    FILE *f=fopen(path,"w");int i;
    if(!f)return;
    fprintf(f,"{\"total_ticks\":%lu,\"events\":[",input_tick);
    for(i=0;i<event_count;i++)fprintf(f,"%s{\"tick\":%lu,\"held\":%u}",i?",":"",events[i].tick,events[i].held);
    fprintf(f,"],\"checkpoints\":[");
    for(i=0;i<check_count;i++){DenCheckpoint *c=&checks[i];
        fprintf(f,"%s{\"tick\":%lu,\"hash\":%u,\"screen\":%d,\"hp\":%d,\"quest\":%d,\"kills\":%d,\"px\":%d,\"py\":%d,\"area\":%d,\"rewarded\":%d,\"skill_points\":%d,\"weapon\":%d,\"shield\":%d,\"torso\":%d,\"items\":%d}",i?",":"",c->tick,c->hash,c->screen,c->hp,c->quest,c->kills,c->px,c->py,c->area,c->rewarded,c->skill_points,c->weapon,c->shield,c->torso,c->items);
    }
    fprintf(f,"]}\n");fclose(f);
}
static int fail(int code,const char *msg){fprintf(stderr,"den run failed (%d): %s\n",code,msg);return code;}

int main(int argc,char **argv)
{
    const char *trace=NULL;int guard=0,fixture_result,points_before_exit,points_before_reward;
    if(argc==3&&strcmp(argv[1],"--trace")==0)trace=argv[2];
    fixture_result=den_self_test();
    if(fixture_result)return fail(1,"simulation unit fixtures");
    /* Save/load fixture uses controller input, then a boot reset. */
    previous=0;input_tick=0;
    den_init();input_frame(KEY_A);input_frame(0);press(KEY_START);press(KEY_B);press(KEY_START);press(KEY_A);
    if(d.menu||d.screen!=DEN_PLAY)return fail(4,"pause/save/resume controls");
    den_init();press(KEY_SELECT);
    if(d.screen!=DEN_PLAY||d.mob_count!=37||d.quest!=DEN_QUEST_CLEAR)return fail(5,"title load restores save");
    /* Fresh trace boots in Rogue Encampment, accepts Akara's offer, enters
       the Den, retrieves the guaranteed Corpsefire armor, and equips it. */
    den_init();previous=0;input_tick=0;event_count=0;check_count=0;gameplay_frames=0;event_overflow=0;
    record_checkpoint();press(KEY_A);
    if(d.screen!=DEN_PLAY||d.area!=DEN_AREA_TOWN||d.quest!=DEN_QUEST_CLEAR)return fail(2,"new-game controller start in town");
    if(!walk_to_akara())return fail(3,"walk to Akara before quest");
    press(KEY_A);if(d.dialog!=DEN_DIALOG_OFFER)return fail(6,"Akara offers Den quest");
    press(KEY_A);if(!d.quest_started||d.dialog!=DEN_DIALOG_ACTIVE)return fail(7,"accept Akara quest");
    press(KEY_B);if(d.dialog)return fail(8,"close quest dialog");
    if(!walk_to_tile(DEN_GATE_X,DEN_GATE_Y,500))return fail(9,"walk to Rogue Encampment gate");
    press(KEY_A);if(d.area!=DEN_AREA_CAVE)return fail(10,"gate enters Den");
    if(d.quest==DEN_QUEST_DONE||d.skill_points)return fail(11,"no reward before clearing Den");
    while(d.quest==DEN_QUEST_CLEAR&&guard++<30000) {
        int target=nearest_alive();
        if(target<0)break;
        if(!route_to_target(target))return fail(12,"controller route to required enemy");
        if(gameplay_frames>40000)return fail(13,"bounded combat replay");
    }
    if(d.quest!=DEN_QUEST_RETURN||den_live_required())return fail(14,"all required enemies trigger return objective");
    if(!pickup_and_equip_torso())return fail(15,"pick up and equip guaranteed Corpsefire armor");
    if(d.screen!=DEN_PLAY||d.area!=DEN_AREA_CAVE||d.quest!=DEN_QUEST_RETURN)return fail(16,"loot keeps player in cleared cave");
    if(!walk_to_tile(DEN_ENTRY_X,DEN_ENTRY_Y,2500))return fail(17,"walk to cave return gate");
    points_before_exit=d.skill_points;press(KEY_A);
    if(d.area!=DEN_AREA_TOWN||d.skill_points!=points_before_exit||d.rewarded)return fail(18,"cave exit does not grant reward");
    if(!walk_to_akara())return fail(19,"walk back to Akara");
    press(KEY_A);if(d.dialog!=DEN_DIALOG_REWARD)return fail(20,"Akara offers turn-in after return");
    points_before_reward=d.skill_points;press(KEY_A);
    if(d.quest!=DEN_QUEST_DONE||!d.rewarded||d.skill_points!=points_before_reward+1||d.screen!=DEN_PLAY)return fail(21,"Akara turn-in grants one skill point in town");
    press(KEY_A);press(KEY_A);
    if(d.skill_points!=points_before_reward+1||d.quest!=DEN_QUEST_DONE||d.screen!=DEN_PLAY)return fail(22,"turn-in reward is one-time");
    if(d.gear.equipment[DEN_SLOT_TORSO]==0||d.gear.items[d.gear.equipment[DEN_SLOT_TORSO]-1].base_id!=DEN_ITEM_QUILTED_ARMOR)return fail(23,"equipped armor persists at turn-in");
    if(!check_count || checks[check_count-1].tick!=input_tick)record_checkpoint();
    if(event_overflow)return fail(24,"controller RLE capacity");
    if(trace)write_trace(trace);
    printf("den completion: controller replay accepted Akara's quest, cleared the authored pack, equipped a normal drop, and received +1 skill point at Akara (%d input events, %d gameplay frames, %lu ticks)\n",event_count,gameplay_frames,input_tick);
    return 0;
}
