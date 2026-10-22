#ifndef D2_DEN_SIM_H
#define D2_DEN_SIM_H

#include "../common/gba.h"
#include "den_items.h"

/* Optional one-class Den of Evil slice. Layout and tuning are authored;
   the five linked cave chambers are not an extracted Diablo II map. */
#define DEN_MAP_W 64
#define DEN_MAP_H 64
#define DEN_MOB_MAX 48
#define DEN_ENTRY_X 8
#define DEN_ENTRY_Y 48
#define DEN_SAVE_TAG 0x44454E31u
#define DEN_SAVE_VERSION 2u
#define DEN_LOOT_MAX 48
#define DEN_TOWN_W 32
#define DEN_TOWN_H 32
#define DEN_AKARA_X 9
#define DEN_AKARA_Y 12
#define DEN_GATE_X 27
#define DEN_GATE_Y 22
#define DEN_MOVE_ONE 1
#define DEN_MELEE_RANGE 13
#define DEN_MOB_MELEE_RANGE 9
#define DEN_BASH_RANGE 15
#define DEN_BASH_COST 2
#define DEN_BASH_FLAT 1

enum { DEN_AREA_TOWN, DEN_AREA_CAVE };
enum { DEN_DIALOG_NONE, DEN_DIALOG_OFFER, DEN_DIALOG_ACTIVE, DEN_DIALOG_REWARD, DEN_DIALOG_THANKS };
enum { DEN_LOOT_ITEM, DEN_LOOT_GOLD, DEN_LOOT_POTION };
enum { DEN_TITLE, DEN_PLAY, DEN_DEAD, DEN_WIN };
enum { DEN_QUEST_CLEAR = 0, DEN_QUEST_RETURN = 1, DEN_QUEST_DONE = 2 };
enum { DEN_ZOMBIE = 0, DEN_FALLEN = 1, DEN_SHAMAN = 2 };
enum { DEN_N_NONE, DEN_N_INTRO, DEN_N_BASH, DEN_N_NO_MANA, DEN_N_NO_POTION,
       DEN_N_CLEAR, DEN_N_RETURN, DEN_N_REWARD, DEN_N_SAVED,
       DEN_N_LOADED, DEN_N_PAUSED, DEN_N_RESUME, DEN_N_DEAD, DEN_N_PICKUP,
       DEN_N_FULL, DEN_N_EQUIPPED, DEN_N_REQUIREMENTS, DEN_N_TOWN, DEN_N_DROPPED };

typedef struct {
    s16 x, y;                 /* eighth-subtile world coordinates */
    s16 hp, max_hp;
    u8 kind, alive, elite, anim, anim_tick, dir;
    u8 corpse, flash, required, revives;
    s16 home_x, home_y;       /* finite aggro leash */
    u16 corpse_serial, last_flee_serial;
} DenMob;

typedef struct {
    DenItem item;
    s16 x,y;
    u16 amount;
    u8 kind,active,area;
} DenGroundItem;

typedef struct {
    int screen;
    u32 tick;
    s16 px, py, hp, max_hp, mana, max_mana;
    u16 level, xp, gold, potions, skill_points, stat_points, corpse_serial;
    u8 bash_rank;
    u16 str, dex, vit, energy, kills;
    u8 quest, notice, menu, inventory, aim, animation;
    u16 notice_time, anim_tick;
    u8 automap, loading, discovered[DEN_MAP_H][DEN_MAP_W];
    u8 map[DEN_MAP_H][DEN_MAP_W]; /* 0 solid, 1 floor, 2 entry */
    DenMob mobs[DEN_MOB_MAX];
    u8 mob_count, bash_cooldown, attack_cooldown, path_budget;
    u8 flee_timer[DEN_MOB_MAX];
    u32 rng;
    /* Append-only prefix preserves the v1 SRAM layout for migration. */
    DenItemState gear;
    DenGroundItem loot[DEN_LOOT_MAX];
    u8 loot_dropped[DEN_MOB_MAX];
    u8 area,inv_cursor,equip_cursor,stat_cursor,dialog,quest_started,rewarded;
    u8 area_serial;
} DenState;

static DenState d;
static s16 den_path_x[DEN_MAP_W*DEN_MAP_H],den_path_y[DEN_MAP_W*DEN_MAP_H];
static u8 den_path_seen[DEN_MAP_H][DEN_MAP_W];
static s8 den_path_dir[DEN_MAP_H][DEN_MAP_W];

typedef struct {
    u32 magic;
    u16 version, size;
    DenState state;
} DenSave;
/* SRAM payloads must not consume the small IWRAM stack. */
static DenSave den_save_buffer;
_Static_assert(sizeof(DenSave)<=16368,"Den save exceeds one journal record");
_Static_assert(offsetof(DenState,gear)+8==9564,"v1 save migration prefix changed");

static int den_abs(int n) { return n < 0 ? -n : n; }
static int den_min(int a, int b) { return a < b ? a : b; }
static int den_max(int a, int b) { return a > b ? a : b; }
static int den_tx(int wx) { return wx >> 3; }
static int den_ty(int wy) { return wy >> 3; }
static int den_distance(int x, int y, int a, int b)
{ return den_abs(x-a) + den_abs(y-b); }
static u8 den_facing(int dx,int dy)
{
    int sx=dx-dy,sy=dx+dy;
    if(den_abs(sx)>2*den_abs(sy))return sx>0?2:6;
    if(den_abs(sy)>2*den_abs(sx))return sy>0?4:0;
    if(sx>=0&&sy<0)return 1;
    if(sx>0&&sy>=0)return 3;
    if(sx<=0&&sy>0)return 5;
    return 7;
}
static u32 den_random(void)
{
    d.rng ^= d.rng << 13; d.rng ^= d.rng >> 17; d.rng ^= d.rng << 5;
    return d.rng;
}
static int den_town_floor(int x,int y)
{
    if(x<2||y<2||x>=30||y>=30)return 0;
    /* Authored camp layout; tents and the central fire have solid footprints. */
    if((x>=5&&x<=7&&y>=7&&y<=10)||(x>=19&&x<=23&&y>=8&&y<=11)||
       (x>=6&&x<=9&&y>=24&&y<=26)||(x>=17&&x<=18&&y>=17&&y<=18))return 0;
    return 1;
}
static int den_cave_floor(int x,int y)
{ return x>=0&&y>=0&&x<DEN_MAP_W&&y<DEN_MAP_H&&d.map[y][x]!=0; }
static int den_floor(int x, int y)
{ return d.area==DEN_AREA_TOWN?den_town_floor(x,y):den_cave_floor(x,y); }
static int den_live_required(void)
{
    int i,n=0; for(i=0;i<d.mob_count;i++) if(d.mobs[i].alive && d.mobs[i].required) ++n;
    return n;
}
static void den_notice(int id, int frames)
{ d.notice=(u8)id; d.notice_time=(u16)frames; }
static const char *den_notice_text(void)
{
    switch(d.notice) {
    case DEN_N_INTRO: return "ROGUE ENCAMPMENT - FIND AKARA";
    case DEN_N_BASH: return "BASH";
    case DEN_N_NO_MANA: return "NOT ENOUGH MANA";
    case DEN_N_NO_POTION: return "NO POTIONS";
    case DEN_N_CLEAR: return "AREA CLEARED";
    case DEN_N_RETURN: return "DEN CLEARED - RETURN TO AKARA";
    case DEN_N_REWARD: return "QUEST COMPLETE: +1 SKILL POINT";
    case DEN_N_SAVED: return "SAVED";
    case DEN_N_LOADED: return "LOADED";
    case DEN_N_PAUSED: return "PAUSED  A:SAVE  B:RESUME";
    case DEN_N_RESUME: return "RESUMED";
    case DEN_N_DEAD: return "YOU HAVE DIED";
    case DEN_N_PICKUP: return "PICKED UP";
    case DEN_N_FULL: return "NOT ENOUGH INVENTORY ROOM";
    case DEN_N_EQUIPPED: return "EQUIPMENT CHANGED";
    case DEN_N_REQUIREMENTS: return "REQUIREMENTS NOT MET";
    case DEN_N_TOWN: return "ROGUE ENCAMPMENT";
    case DEN_N_DROPPED: return "ITEM DROPPED";
    default: return "";
    }
}
static u32 den_hash_byte(u32 h,u8 v){return (h^v)*16777619u;}
static u32 den_hash_u16(u32 h,u16 v){h=den_hash_byte(h,(u8)v);return den_hash_byte(h,(u8)(v>>8));}
static u32 den_hash_s16(u32 h,s16 v){return den_hash_u16(h,(u16)v);}
static u32 den_state_hash(void)
{
    u32 h=2166136261u;int i,y,x;
    h=den_hash_u16(h,(u16)d.screen);h=den_hash_u16(h,(u16)d.tick);h=den_hash_s16(h,d.px);h=den_hash_s16(h,d.py);
    h=den_hash_s16(h,d.hp);h=den_hash_s16(h,d.max_hp);h=den_hash_s16(h,d.mana);h=den_hash_s16(h,d.max_mana);
    h=den_hash_u16(h,d.level);h=den_hash_u16(h,d.xp);h=den_hash_u16(h,d.gold);h=den_hash_u16(h,d.potions);
    h=den_hash_u16(h,d.skill_points);h=den_hash_u16(h,d.stat_points);h=den_hash_u16(h,d.kills);
    h=den_hash_u16(h,d.str);h=den_hash_u16(h,d.dex);h=den_hash_u16(h,d.vit);h=den_hash_u16(h,d.energy);
    h=den_hash_u16(h,d.corpse_serial);h=den_hash_byte(h,d.bash_rank);h=den_hash_byte(h,d.quest);h=den_hash_byte(h,d.notice);
    h=den_hash_u16(h,d.notice_time);h=den_hash_u16(h,d.anim_tick);
    h=den_hash_byte(h,d.menu);h=den_hash_byte(h,d.inventory);h=den_hash_byte(h,d.aim);h=den_hash_byte(h,d.animation);
    h=den_hash_byte(h,d.automap);h=den_hash_byte(h,d.mob_count);
    h=den_hash_byte(h,d.loading);h=den_hash_byte(h,d.bash_cooldown);h=den_hash_byte(h,d.attack_cooldown);
    h=den_hash_byte(h,d.path_budget);h=den_hash_u16(h,(u16)d.rng);h=den_hash_u16(h,(u16)(d.rng>>16));
    for(y=0;y<DEN_MAP_H;y++)for(x=0;x<DEN_MAP_W;x++)h=den_hash_byte(h,d.discovered[y][x]);
    for(y=0;y<DEN_MAP_H;y++)for(x=0;x<DEN_MAP_W;x++)h=den_hash_byte(h,d.map[y][x]);
    for(i=0;i<d.mob_count;i++){DenMob *m=&d.mobs[i];
        h=den_hash_s16(h,m->x);h=den_hash_s16(h,m->y);h=den_hash_s16(h,m->hp);h=den_hash_s16(h,m->max_hp);
        h=den_hash_byte(h,m->kind);h=den_hash_byte(h,m->alive);h=den_hash_byte(h,m->elite);h=den_hash_byte(h,m->anim);
        h=den_hash_byte(h,m->anim_tick);h=den_hash_byte(h,m->dir);h=den_hash_byte(h,m->corpse);h=den_hash_byte(h,m->flash);
        h=den_hash_byte(h,m->required);h=den_hash_byte(h,m->revives);h=den_hash_s16(h,m->home_x);h=den_hash_s16(h,m->home_y);
        h=den_hash_u16(h,m->corpse_serial);h=den_hash_u16(h,m->last_flee_serial);
        h=den_hash_byte(h,d.flee_timer[i]);
    }
    h=den_hash_byte(h,d.area);h=den_hash_byte(h,d.inv_cursor);h=den_hash_byte(h,d.equip_cursor);
    h=den_hash_byte(h,d.stat_cursor);h=den_hash_byte(h,d.dialog);h=den_hash_byte(h,d.quest_started);
    h=den_hash_byte(h,d.rewarded);h=den_hash_byte(h,d.area_serial);
    for(i=0;i<DEN_ITEM_RECORDS;i++){DenItem *a=&d.gear.items[i];
        h=den_hash_byte(h,a->base_id);h=den_hash_byte(h,a->quality);h=den_hash_byte(h,a->durability);h=den_hash_byte(h,a->flags);
        h=den_hash_u16(h,a->defense_roll);h=den_hash_u16(h,a->seed);}
    for(i=0;i<DEN_INVENTORY_CELLS;i++)h=den_hash_byte(h,d.gear.grid[i]);
    for(i=0;i<DEN_EQUIP_SLOTS;i++)h=den_hash_byte(h,d.gear.equipment[i]);
    for(i=0;i<DEN_LOOT_MAX;i++){DenGroundItem *a=&d.loot[i];
        h=den_hash_byte(h,a->active);h=den_hash_byte(h,a->kind);h=den_hash_byte(h,a->area);
        h=den_hash_s16(h,a->x);h=den_hash_s16(h,a->y);h=den_hash_u16(h,a->amount);
        h=den_hash_byte(h,a->item.base_id);h=den_hash_byte(h,a->item.quality);h=den_hash_byte(h,a->item.durability);
        h=den_hash_byte(h,a->item.flags);h=den_hash_u16(h,a->item.defense_roll);h=den_hash_u16(h,a->item.seed);}
    for(i=0;i<DEN_MOB_MAX;i++)h=den_hash_byte(h,d.loot_dropped[i]);
    return h;
}
static int den_max_hp_value(void)
{ return 55+(d.level-1)*2+(d.vit-25)*4; }
static int den_max_mana_value(void)
{ return 10+(d.level-1)+(d.energy-10); }
static void den_carve_room(int cx,int cy,int rx,int ry)
{
    int x,y;
    for(y=cy-ry;y<=cy+ry;y++) for(x=cx-rx;x<=cx+rx;x++)
        if(x>0&&y>0&&x<DEN_MAP_W-1&&y<DEN_MAP_H-1) d.map[y][x]=1;
}
static void den_carve_hall(int x0,int y0,int x1,int y1)
{
    int x=x0,y=y0,step;
    while(x!=x1) { d.map[y][x]=1; if(y>0&&y<DEN_MAP_H-1)d.map[y-1][x]=1; x += x<x1?1:-1; }
    while(y!=y1) { d.map[y][x]=1; if(x>0&&x<DEN_MAP_W-1)d.map[y][x-1]=1; y += y<y1?1:-1; }
    for(step=0;step<2;step++) d.map[y][x]=1;
}
static void den_build_map(void)
{
    int y,x;
    for(y=0;y<DEN_MAP_H;y++) for(x=0;x<DEN_MAP_W;x++) d.map[y][x]=0;
    /* Five authored linked chambers, with narrow orthogonal halls. */
    den_carve_room(8,48,5,5);    /* entry */
    den_carve_room(20,43,5,5);   /* west chamber */
    den_carve_room(33,35,5,5);   /* central chamber */
    den_carve_room(45,26,5,5);   /* north branch */
    den_carve_room(51,13,6,5);   /* Corpsefire chamber */
    den_carve_hall(13,47,16,45);
    den_carve_hall(24,41,29,38);
    den_carve_hall(37,33,41,29);
    den_carve_hall(47,21,50,18);
    d.map[DEN_ENTRY_Y][DEN_ENTRY_X]=2;
    for(y=0;y<DEN_MAP_H;y++) for(x=0;x<DEN_MAP_W;x++) d.discovered[y][x]=0;
}
static void den_spawn(int kind,int tx,int ty,int elite)
{
    DenMob *m;
    if(d.mob_count>=DEN_MOB_MAX) return;
    m=&d.mobs[d.mob_count++];
    m->x=(s16)(tx*8+4); m->y=(s16)(ty*8+4);
    /* Demo 1.04 normal difficulty ranges: Fallen 1-3, Zombie 5-9,
       Shaman 4-7. Corpsefire is intentionally authored as a small boss. */
    m->hp=m->max_hp=(s16)(kind==DEN_ZOMBIE?5+(den_random()%5):
                             kind==DEN_SHAMAN?4+(den_random()%4):1+(den_random()%3));
    if(elite) m->hp=m->max_hp=32;
    m->kind=(u8)kind; m->alive=1; m->elite=(u8)elite;
    m->anim=0; m->anim_tick=0; m->dir=0; m->corpse=0; m->flash=0;
    m->required=1; m->revives=0; m->home_x=m->x; m->home_y=m->y;
    m->corpse_serial=0; m->last_flee_serial=0;
}
static void den_spawn_pack(void)
{
    d.mob_count=0;
    /* Authored tutorial pack: 37 required enemies, including Corpsefire.
       Counts are prototype tuning, not an asserted original-game roster. */
    den_spawn(DEN_FALLEN,17,45,0); den_spawn(DEN_FALLEN,19,42,0);
    den_spawn(DEN_FALLEN,22,46,0); den_spawn(DEN_ZOMBIE,20,40,0);
    den_spawn(DEN_SHAMAN,24,43,0); den_spawn(DEN_FALLEN,17,41,0);
    den_spawn(DEN_FALLEN,22,39,0); den_spawn(DEN_ZOMBIE,25,46,0);
    den_spawn(DEN_FALLEN,29,39,0); den_spawn(DEN_FALLEN,31,37,0);
    den_spawn(DEN_FALLEN,34,39,0); den_spawn(DEN_ZOMBIE,36,35,0);
    den_spawn(DEN_SHAMAN,32,33,0); den_spawn(DEN_FALLEN,30,32,0);
    den_spawn(DEN_FALLEN,35,32,0); den_spawn(DEN_ZOMBIE,38,37,0);
    den_spawn(DEN_FALLEN,41,28,0); den_spawn(DEN_FALLEN,43,25,0);
    den_spawn(DEN_ZOMBIE,45,23,0); den_spawn(DEN_SHAMAN,46,29,0);
    den_spawn(DEN_FALLEN,48,24,0); den_spawn(DEN_FALLEN,42,21,0);
    den_spawn(DEN_ZOMBIE,48,28,0); den_spawn(DEN_FALLEN,51,15,0);
    den_spawn(DEN_FALLEN,54,13,0); den_spawn(DEN_ZOMBIE,55,10,0);
    den_spawn(DEN_FALLEN,49,12,0); den_spawn(DEN_SHAMAN,53,16,0);
    den_spawn(DEN_FALLEN,48,16,0); den_spawn(DEN_ZOMBIE,56,17,0);
    den_spawn(DEN_FALLEN,52,9,0); den_spawn(DEN_FALLEN,46,13,0);
    den_spawn(DEN_ZOMBIE,51,18,0); den_spawn(DEN_FALLEN,56,14,0);
    den_spawn(DEN_ZOMBIE,53,11,0); den_spawn(DEN_FALLEN,50,10,0);
    den_spawn(DEN_ZOMBIE,51,13,1); /* Corpsefire, Den Zombie super unique */
}
static void den_starting_gear(void)
{
    DenItem item;int index;
    den_items_reset(&d.gear);
    den_item_make(&item,DEN_ITEM_HAND_AXE,1);den_inventory_add(&d.gear,item,&index);
    den_item_equip(&d.gear,index,DEN_SLOT_WEAPON,d.str,d.dex,d.level);
    den_item_make(&item,DEN_ITEM_BUCKLER,2);den_inventory_add(&d.gear,item,&index);
    den_item_equip(&d.gear,index,DEN_SLOT_SHIELD,d.str,d.dex,d.level);
}
static void den_enter_area(int area)
{
    d.area=(u8)area;++d.area_serial;d.dialog=0;d.inventory=0;d.animation=0;d.anim_tick=0;
    if(area==DEN_AREA_TOWN){d.px=DEN_GATE_X*8-12;d.py=DEN_GATE_Y*8+4;den_notice(DEN_N_TOWN,70);}
    else {d.px=DEN_ENTRY_X*8+4;d.py=DEN_ENTRY_Y*8+4;den_notice(DEN_N_CLEAR,30);}
}
static void den_start_new(void)
{
    int i;
    d=(DenState){0};
    d.tick=0; d.screen=DEN_PLAY; d.quest=DEN_QUEST_CLEAR;
    d.level=1; d.xp=0; d.gold=0; d.potions=3; d.skill_points=0; d.stat_points=0;
    d.str=30; d.dex=20; d.vit=25; d.energy=10; d.bash_rank=1; d.corpse_serial=0;
    d.max_hp=55; d.hp=d.max_hp; d.max_mana=10; d.mana=d.max_mana;
    d.kills=0; d.menu=0; d.inventory=0; d.notice=DEN_N_NONE; d.notice_time=0;
    d.aim=1; d.animation=0; d.anim_tick=0; d.automap=0;
    d.bash_cooldown=d.attack_cooldown=d.path_budget=0;
    for(i=0;i<DEN_MOB_MAX;i++) d.flee_timer[i]=0;
    d.px=DEN_ENTRY_X*8+4; d.py=DEN_ENTRY_Y*8+4; d.rng=0x9e3779b9u;
    den_build_map(); den_spawn_pack();den_starting_gear();
    d.area=DEN_AREA_TOWN;d.px=12*8+4;d.py=22*8+4;
    den_notice(DEN_N_INTRO,100);
}
static int den_valid_item(DenItem a)
{
    const DenItemDef *def=den_item_def(a.base_id);
    return def&&a.quality==DEN_ITEM_QUALITY_NORMAL&&a.durability<=def->max_durability&&
        a.defense_roll>=def->min_defense&&a.defense_roll<=def->max_defense;
}
static int den_valid_gear(const DenItemState *g)
{
    u8 used[DEN_ITEM_RECORDS]={0};int i,j,x,y,at;
    for(i=0;i<DEN_EQUIP_SLOTS;i++)if(g->equipment[i]){
        int k=g->equipment[i]-1;
        if(k>=DEN_ITEM_RECORDS||used[k]||!den_valid_item(g->items[k])||den_item_def(g->items[k].base_id)->equip_slot!=i)return 0;
        used[k]=1;
    }
    for(i=0;i<DEN_INVENTORY_CELLS;i++)if(g->grid[i]){
        int k=g->grid[i]-1;const DenItemDef *def;
        if(k>=DEN_ITEM_RECORDS||!den_valid_item(g->items[k]))return 0;
        if(used[k]){if(used[k]==1)return 0;continue;}
        def=den_item_def(g->items[k].base_id);at=i;
        if(at%10+def->width>10||at/10+def->height>4)return 0;
        for(y=0;y<def->height;y++)for(x=0;x<def->width;x++)if(g->grid[at+y*10+x]!=k+1)return 0;
        for(j=0;j<40;j++)if(g->grid[j]==k+1&&!(j/10>=at/10&&j/10<at/10+def->height&&j%10>=at%10&&j%10<at%10+def->width))return 0;
        used[k]=2;
    }
    for(i=0;i<DEN_ITEM_RECORDS;i++)if((g->items[i].base_id!=0)!=(used[i]!=0))return 0;
    return 1;
}
static void den_save(void)
{
    DenSave *s=&den_save_buffer;
    s->magic=0x44454E53u;s->version=DEN_SAVE_VERSION;s->size=(u16)sizeof(*s);s->state=d;
    if(s->state.screen==DEN_DEAD){s->state.screen=DEN_PLAY;s->state.hp=1;}
    s->state.menu=0;s->state.inventory=0;s->state.dialog=0;
    save_write(s,sizeof(*s),DEN_SAVE_TAG);den_notice(DEN_N_SAVED,50);
}
static int den_load(void)
{
    DenSave *s=&den_save_buffer;int legacy=0,i;
    *s=(DenSave){0};
    if(!save_read(s,sizeof(*s),DEN_SAVE_TAG)){
        unsigned old_size=8+offsetof(DenState,gear);
        if(!save_read(s,old_size,DEN_SAVE_TAG)||s->version!=1||s->size!=old_size)return 0;
        legacy=1;
    }else if(s->version!=DEN_SAVE_VERSION||s->size!=sizeof(*s))return 0;
    if(s->magic!=0x44454E53u||s->state.mob_count>DEN_MOB_MAX||s->state.quest>DEN_QUEST_DONE||
       (s->state.screen!=DEN_PLAY&&s->state.screen!=DEN_WIN)||s->state.level<1||s->state.level>99||
       s->state.hp<1||s->state.max_hp<1||s->state.hp>s->state.max_hp||s->state.mana<0||
       s->state.mana>s->state.max_mana||s->state.bash_rank>20||s->state.potions>999||
       s->state.str<1||s->state.str>1000||s->state.dex<1||s->state.dex>1000||
       s->state.vit<25||s->state.vit>1000||s->state.energy<10||s->state.energy>1000)return 0;
    if(!legacy){
        if(s->state.area>DEN_AREA_CAVE||s->state.dialog>DEN_DIALOG_THANKS||s->state.inventory>3||
           s->state.inv_cursor>=40||s->state.equip_cursor>=7||s->state.stat_cursor>=5||
           s->state.rewarded>1||s->state.quest_started>1||
           ((s->state.quest==DEN_QUEST_DONE)!=(s->state.rewarded!=0))||!den_valid_gear(&s->state.gear))return 0;
        for(i=0;i<DEN_LOOT_MAX;i++){DenGroundItem *g=&s->state.loot[i];if(g->active&&
            (g->kind>DEN_LOOT_POTION||g->area>DEN_AREA_CAVE||g->x<0||g->y<0||g->x>=512||g->y>=512||
             (g->kind==DEN_LOOT_ITEM&&!den_valid_item(g->item))))return 0;}
    }
    for(i=0;i<s->state.mob_count;i++){DenMob *m=&s->state.mobs[i];
        if(m->kind>DEN_SHAMAN||m->x<0||m->x>=512||m->y<0||m->y>=512||m->alive>1)return 0;}
    {
        int x=den_tx(s->state.px),y=den_ty(s->state.py);
        if(x<0||y<0||x>=DEN_MAP_W||y>=DEN_MAP_H)return 0;
        if(!legacy&&s->state.area==DEN_AREA_TOWN){if(!den_town_floor(x,y))return 0;}
        else if(!s->state.map[y][x])return 0;
    }
    d=s->state;
    if(legacy){den_starting_gear();d.quest_started=1;d.rewarded=d.quest==DEN_QUEST_DONE;
        d.area=DEN_AREA_CAVE;if(d.rewarded)den_enter_area(DEN_AREA_TOWN);
        for(i=0;i<d.mob_count;i++)d.loot_dropped[i]=!d.mobs[i].alive;}
    d.screen=DEN_PLAY;d.menu=0;d.inventory=0;d.dialog=0;++d.area_serial;
    den_notice(DEN_N_LOADED,60);return 1;
}
static int den_free_loot(void)
{int i;for(i=0;i<DEN_LOOT_MAX;i++)if(!d.loot[i].active)return i;return -1;}
static int den_nearest_loot(int range)
{
    int i,best=-1,dist=range+1;
    for(i=0;i<DEN_LOOT_MAX;i++)if(d.loot[i].active&&d.loot[i].area==d.area){
        int n=den_distance(d.px,d.py,d.loot[i].x,d.loot[i].y);if(n<dist){dist=n;best=i;}}
    return best;
}
static void den_drop_for_mob(DenMob *m)
{
    int mob=(int)(m-d.mobs),slot;u32 roll;DenGroundItem *g;
    if(d.loot_dropped[mob])return;
    slot=den_free_loot();if(slot<0)return;
    d.loot_dropped[mob]=1;
    roll=den_random();g=&d.loot[slot];*g=(DenGroundItem){0};
    g->active=1;g->area=DEN_AREA_CAVE;g->x=m->x;g->y=m->y;
    /* Bounded, authored tutorial drops, not the full TreasureClass engine. */
    if(m->elite||roll%5==0){
        static const u8 bases[]={DEN_ITEM_CAP,DEN_ITEM_QUILTED_ARMOR,DEN_ITEM_LEATHER_BOOTS,
            DEN_ITEM_LEATHER_GLOVES,DEN_ITEM_SASH,DEN_ITEM_CLUB,DEN_ITEM_SCIMITAR};
        g->kind=DEN_LOOT_ITEM;den_item_make(&g->item,bases[m->elite?1:(roll>>8)%7],(u16)(roll>>16));
    }else if(roll%5==1){g->kind=DEN_LOOT_POTION;g->amount=1;}
    else {g->kind=DEN_LOOT_GOLD;g->amount=(u16)(1+(roll>>8)%8);}
}
static void den_pickup(int n)
{
    DenGroundItem *g=&d.loot[n];
    if(g->kind==DEN_LOOT_ITEM){if(!den_inventory_add(&d.gear,g->item,0)){den_notice(DEN_N_FULL,50);return;}}
    else if(g->kind==DEN_LOOT_GOLD)d.gold=(u16)den_min(65535,(int)d.gold+g->amount);
    else d.potions=(u16)den_min(999,d.potions+g->amount);
    g->active=0;den_notice(DEN_N_PICKUP,30);
}
static void den_drop_inventory(int index)
{
    int slot=den_free_loot();DenGroundItem *g;
    if(index<0||index>=DEN_ITEM_RECORDS||!d.gear.items[index].base_id||slot<0){den_notice(DEN_N_FULL,40);return;}
    g=&d.loot[slot];*g=(DenGroundItem){0};g->item=d.gear.items[index];
    if(!den_inventory_remove(&d.gear,index))return;
    g->active=1;g->kind=DEN_LOOT_ITEM;g->area=d.area;g->x=d.px;g->y=d.py;den_notice(DEN_N_DROPPED,30);
}
static void den_kill(DenMob *m)
{
    u32 threshold;
    if(!m->alive) return;
    m->alive=0; m->corpse=1; m->anim=4; m->anim_tick=0;
    ++d.corpse_serial; m->corpse_serial=d.corpse_serial;
    ++d.kills; d.xp=(u16)(d.xp+(m->elite?120:m->kind==DEN_ZOMBIE?33:m->kind==DEN_SHAMAN?32:18));
    den_drop_for_mob(m);
    threshold=200u*d.level;
    while(d.xp>=threshold) {
        d.xp=(u16)(d.xp-threshold); ++d.level; ++d.skill_points; d.stat_points=(u16)(d.stat_points+5);
        d.max_hp=(s16)den_max_hp_value(); d.hp=d.max_hp;
        d.max_mana=(s16)den_max_mana_value(); d.mana=d.max_mana;
        threshold=200u*d.level;
    }
    den_notice(DEN_N_NONE,18);
    if(d.quest==DEN_QUEST_CLEAR && !den_live_required()) {
        d.quest=DEN_QUEST_RETURN; den_notice(DEN_N_RETURN,180);
    }
}
static int den_hit_chance(const DenMob *m,int bash)
{
    DenItemStats stats;int ar;
    den_item_stats(&d.gear,d.str,d.dex,d.level,&stats);ar=stats.attack_rating;
    if(bash)ar=ar*(115+5*d.bash_rank)/100;
    return den_item_hit_chance(ar,5+(m->elite?8:0),d.level,1);
}
static int den_nearest_mob(int range)
{
    int i,best=-1,bestdist=range+1;
    for(i=0;i<d.mob_count;i++) if(d.mobs[i].alive) {
        int dist=den_distance(d.px,d.py,d.mobs[i].x,d.mobs[i].y);
        if(dist<bestdist){bestdist=dist;best=i;}
    }
    return best;
}
/* D2MOO Items.cpp ITEMS_UpdateDurability: 4% weapons, 10% armor.
   SUnitDmg.cpp SUNITDMG_DrainItemDurability supplies body-location weights. */
static void den_wear_slot(int slot,int chance)
{
    int k=d.gear.equipment[slot];
    if(k&&d.gear.items[k-1].durability&&den_random()%100<(u32)chance)--d.gear.items[k-1].durability;
}
static void den_wear_armor(void)
{
    static const u8 slots[]={DEN_SLOT_HEAD,DEN_SLOT_TORSO,DEN_SLOT_SHIELD,DEN_SLOT_BELT,DEN_SLOT_BOOTS,DEN_SLOT_GLOVES};
    static const u8 weights[]={3,5,4,2,2,2};int i,total=0,roll;
    for(i=0;i<6;i++)if(d.gear.equipment[slots[i]])total+=weights[i];
    if(!total)return;
    roll=(int)(den_random()%(u32)total);
    for(i=0;i<6;i++)if(d.gear.equipment[slots[i]]){if(roll<weights[i]){den_wear_slot(slots[i],10);return;}roll-=weights[i];}
}
static void den_attack(int bash)
{
    int i=den_nearest_mob(bash?DEN_BASH_RANGE:DEN_MELEE_RANGE),chance,damage;
    DenMob *m;DenItemStats stats;
    if(i<0||d.area!=DEN_AREA_CAVE)return;
    m=&d.mobs[i]; d.aim=den_facing(m->x-d.px,m->y-d.py); chance=den_hit_chance(m,bash);
    if(den_random()%100>=(u32)chance) { den_notice(DEN_N_NONE,18); return; }
    den_item_stats(&d.gear,d.str,d.dex,d.level,&stats);
    if(!stats.weapon_id){stats.min_damage=1;stats.max_damage=2;}
    damage=stats.min_damage+(int)(den_random()%(stats.max_damage-stats.min_damage+1))+(bash?d.bash_rank:0);
    if(bash) damage=damage*(45+5*d.bash_rank)/100+damage;
    d.animation=2; d.anim_tick=0;
    m->hp=(s16)(m->hp-damage); m->flash=8;den_wear_slot(DEN_SLOT_WEAPON,4);
    if(bash) {
        int dx=m->x-d.px,dy=m->y-d.py;
        int nx=m->x+(dx>0?3:dx<0?-3:0),ny=m->y+(dy>0?3:dy<0?-3:0);
        int tx=den_tx(nx),ty=den_ty(ny);
        if(den_floor(tx,ty)){m->x=(s16)nx;m->y=(s16)ny;}
    }
    if(m->hp<=0) den_kill(m);
}
static void den_try_move_player(int dx,int dy)
{
    int nx=d.px+dx,ny=d.py+dy;
    if(den_floor(den_tx(nx),den_ty(ny))){d.px=(s16)nx;d.py=(s16)ny;}
}
static void den_move_mob(DenMob *m,int i)
{
    int dx=d.px-m->x,dy=d.py-m->y,dist=den_abs(dx)+den_abs(dy),mx=0,my=0;
    static const s8 ax[4]={1,-1,0,0},ay[4]={0,0,1,-1};
    if(dist<=DEN_MOB_MELEE_RANGE || dist>11*8 || (d.tick+i*7)%3) return;
    if(den_abs(dx)>=den_abs(dy)) mx=dx>0?1:-1; else my=dy>0?1:-1;
    if(den_floor(den_tx(m->x+mx),den_ty(m->y+my))) {
        m->x=(s16)(m->x+mx);m->y=(s16)(m->y+my);return;
    }
    /* Amortized grid BFS is used only when direct pursuit hits a wall. */
    {
        int head=0,tail=0,sx=den_tx(m->x),sy=den_ty(m->y),gx=den_tx(d.px),gy=den_ty(d.py);
        int k,found=0,y;
        for(y=0;y<DEN_MAP_H;y++){int x;for(x=0;x<DEN_MAP_W;x++){den_path_seen[y][x]=0;den_path_dir[y][x]=-1;}}
        den_path_x[tail]=(s16)sx;den_path_y[tail++]=(s16)sy;den_path_seen[sy][sx]=1;
        while(head<tail&&!found){int x=den_path_x[head],cy=den_path_y[head++];
            for(k=0;k<4;k++){int nx=x+ax[k],ny=cy+ay[k];
                if(den_floor(nx,ny)&&!den_path_seen[ny][nx]){den_path_seen[ny][nx]=1;den_path_dir[ny][nx]=(s8)k;
                    den_path_x[tail]=(s16)nx;den_path_y[tail++]=(s16)ny;if(nx==gx&&ny==gy){found=1;break;}}
            }
        }
        if(found){int x=gx,cy=gy;
            mx=0;my=0;
            while(1){int dir=den_path_dir[cy][x],px=x-ax[dir],py=cy-ay[dir];if(px==sx&&py==sy)break;x=px;cy=py;}
            dx=x*8+4-m->x;dy=cy*8+4-m->y;
            if(den_abs(dx)>=den_abs(dy))mx=dx>0?1:dx<0?-1:0;else my=dy>0?1:dy<0?-1:0;
            if(den_floor(den_tx(m->x+mx),den_ty(m->y+my))){m->x=(s16)(m->x+mx);m->y=(s16)(m->y+my);}
        }
    }
}
static void den_update_mobs(void)
{
    int i;DenItemStats stats;
    if(d.area!=DEN_AREA_CAVE)return;
    den_item_stats(&d.gear,d.str,d.dex,d.level,&stats);
    for(i=0;i<d.mob_count;i++) {
        DenMob *m=&d.mobs[i];
        if(m->flash)m->flash--;
        if(!m->alive) {
            if(m->corpse&&!d.loot_dropped[i])den_drop_for_mob(m);
            if(m->corpse&&m->anim==4&&m->anim_tick<255)++m->anim_tick;
            continue;
        }
        if(m->kind==DEN_FALLEN) {
            int j;
            for(j=0;j<d.mob_count;j++) if(d.mobs[j].corpse && d.mobs[j].corpse_serial==d.corpse_serial &&
                m->last_flee_serial!=d.corpse_serial && den_distance(m->x,m->y,d.mobs[j].x,d.mobs[j].y)<11*8) {
                m->last_flee_serial=d.corpse_serial;d.flee_timer[i]=90;break;
            }
            if(d.flee_timer[i]) {
                int dx=m->x-d.px,dy=m->y-d.py;
                if(den_abs(dx)>=den_abs(dy)) dx=dx>=0?1:-1,dy=0; else dy=dy>=0?1:-1,dx=0;
                if((d.tick+i)&1) { if(den_floor(den_tx(m->x+dx),den_ty(m->y+dy))){m->x=(s16)(m->x+dx);m->y=(s16)(m->y+dy);} }
                --d.flee_timer[i]; continue;
            }
        }
        {
            m->dir=den_facing(d.px-m->x,d.py-m->y);
        }
        if(m->kind==DEN_SHAMAN && (d.tick%90)==(u32)(i%90) && m->revives<2) {
            int j;
            for(j=0;j<d.mob_count;j++) if(d.mobs[j].corpse && d.mobs[j].kind==DEN_FALLEN &&
                den_distance(m->x,m->y,d.mobs[j].x,d.mobs[j].y)<13*8) {
                DenMob *r=&d.mobs[j]; r->corpse=0;r->alive=1;r->hp=(s16)den_max(1,r->max_hp/2);r->anim=0;r->anim_tick=0;++m->revives;break;
            }
        }
        if(m->anim==1) { if(++m->anim_tick>=12)m->anim_tick=0; }
        else if(m->anim==2) { if(++m->anim_tick>=8){m->anim=0;m->anim_tick=0;} }
        else {m->anim=0;m->anim_tick=0;}
        if(den_distance(m->home_x,m->home_y,d.px,d.py)>15*8) continue;
        if(den_distance(m->x,m->y,d.px,d.py)<9*8) {
            if(den_distance(m->x,m->y,d.px,d.py)>9) {m->anim=1;den_move_mob(m,i);}
            else if(den_distance(m->x,m->y,d.px,d.py)<=DEN_MOB_MELEE_RANGE && (d.tick+i*13)%38==0) {
                int damage=1+(int)(den_random()%3)+(m->elite?2:0);
                m->anim=2;
                if(den_random()%100>=(u32)den_item_hit_chance(40,stats.defense+d.dex/4,1,d.level))continue;
                if(stats.block_chance&&den_random()%100<stats.block_chance){den_wear_slot(DEN_SLOT_SHIELD,10);continue;}
                den_wear_armor();d.hp=(s16)(d.hp-damage);
                if(d.hp<=0){d.hp=0;d.screen=DEN_DEAD;d.animation=4;d.anim_tick=0;den_notice(DEN_N_DEAD,180);}
                else {d.animation=3;d.anim_tick=0;}
            }
        } else m->anim=0;
    }
}
static int den_near_akara(void)
{return d.area==DEN_AREA_TOWN&&den_distance(d.px,d.py,DEN_AKARA_X*8+4,DEN_AKARA_Y*8+4)<=20;}
static void den_talk_akara(void)
{
    d.hp=d.max_hp;d.mana=d.max_mana;
    d.dialog=d.rewarded?DEN_DIALOG_THANKS:d.quest==DEN_QUEST_RETURN?DEN_DIALOG_REWARD:
        d.quest_started?DEN_DIALOG_ACTIVE:DEN_DIALOG_OFFER;
}
static void den_dialog_input(u16 pressed)
{
    if(pressed&KEY_B){d.dialog=0;return;}
    if(!(pressed&KEY_A))return;
    if(d.dialog==DEN_DIALOG_OFFER){d.quest_started=1;d.dialog=DEN_DIALOG_ACTIVE;return;}
    /* D2MOO A1Q1 Callback11, Akara message 76: pending -> granted, +1 skill. */
    if(d.dialog==DEN_DIALOG_REWARD&&d.quest==DEN_QUEST_RETURN&&!d.rewarded){
        ++d.skill_points;d.rewarded=1;d.quest=DEN_QUEST_DONE;d.quest_started=1;
        d.dialog=DEN_DIALOG_THANKS;den_save();d.dialog=DEN_DIALOG_THANKS;den_notice(DEN_N_REWARD,180);return;
    }
    d.dialog=0;
}
static void den_inventory_input(u16 pressed)
{
    if(pressed&KEY_SELECT){d.inventory=(u8)(d.inventory%3+1);return;}
    if(d.inventory==1){
        if((pressed&KEY_LEFT)&&d.inv_cursor%10)d.inv_cursor--;
        if((pressed&KEY_RIGHT)&&d.inv_cursor%10<9)d.inv_cursor++;
        if((pressed&KEY_UP)&&d.inv_cursor>=10)d.inv_cursor-=10;
        if((pressed&KEY_DOWN)&&d.inv_cursor<30)d.inv_cursor+=10;
        if(d.gear.grid[d.inv_cursor]){int i=d.gear.grid[d.inv_cursor]-1;
            if(pressed&KEY_B)den_drop_inventory(i);
            if(pressed&KEY_A){const DenItemDef *def=den_item_def(d.gear.items[i].base_id);
                if(!den_item_requirements(&d.gear.items[i],d.str,d.dex,d.level))den_notice(DEN_N_REQUIREMENTS,50);
                else if(!den_item_equip(&d.gear,i,def->equip_slot,d.str,d.dex,d.level))den_notice(DEN_N_FULL,50);
                else den_notice(DEN_N_EQUIPPED,30);}}
    }else if(d.inventory==2){
        if((pressed&(KEY_UP|KEY_LEFT))&&d.equip_cursor)d.equip_cursor--;
        if((pressed&(KEY_DOWN|KEY_RIGHT))&&d.equip_cursor<6)d.equip_cursor++;
        if(pressed&KEY_A){if(den_item_unequip(&d.gear,d.equip_cursor))den_notice(DEN_N_EQUIPPED,30);else den_notice(DEN_N_FULL,40);}
        if(pressed&KEY_B)d.inventory=0;
    }else{
        if((pressed&KEY_UP)&&d.stat_cursor)d.stat_cursor--;
        if((pressed&KEY_DOWN)&&d.stat_cursor<4)d.stat_cursor++;
        if(pressed&KEY_A){
            if(d.stat_cursor==4){if(d.skill_points&&d.bash_rank<20){--d.skill_points;++d.bash_rank;}}
            else if(d.stat_points){--d.stat_points;
                if(d.stat_cursor==0)++d.str;else if(d.stat_cursor==1)++d.dex;
                else if(d.stat_cursor==2){++d.vit;d.max_hp=(s16)den_max_hp_value();d.hp=(s16)den_min(d.max_hp,d.hp+4);}
                else{++d.energy;d.max_mana=(s16)den_max_mana_value();++d.mana;}}
        }
        if(pressed&KEY_B)d.inventory=0;
    }
}
static int den_interact(void)
{
    int loot;
    if(den_near_akara()){den_talk_akara();return 1;}
    if(d.area==DEN_AREA_TOWN&&den_distance(d.px,d.py,DEN_GATE_X*8+4,DEN_GATE_Y*8+4)<=16){den_enter_area(DEN_AREA_CAVE);return 1;}
    if(d.area==DEN_AREA_CAVE&&den_distance(d.px,d.py,DEN_ENTRY_X*8+4,DEN_ENTRY_Y*8+4)<=12){den_enter_area(DEN_AREA_TOWN);return 1;}
    loot=den_nearest_loot(14);if(loot>=0){den_pickup(loot);return 1;}return 0;
}
static void den_update(u16 held,u16 pressed)
{
    ++d.tick;
    if(d.notice_time && --d.notice_time==0)d.notice=DEN_N_NONE;
    if(d.screen==DEN_TITLE) {
        if(pressed&KEY_A) den_start_new();
        if(pressed&KEY_SELECT) { if(!den_load())den_notice(DEN_N_NONE,20); }
        return;
    }
    if(d.screen==DEN_DEAD || d.screen==DEN_WIN) {
        if(d.screen==DEN_DEAD && d.anim_tick<255)++d.anim_tick;
        if(pressed&(KEY_A|KEY_START)) den_start_new();
        else if(pressed&KEY_SELECT) d.screen=DEN_TITLE;
        return;
    }
    if(d.dialog){den_dialog_input(pressed);return;}
    if((pressed&KEY_SELECT)&&!d.inventory)d.automap^=1;
    if(pressed&KEY_START){d.menu^=1;if(d.menu)den_notice(DEN_N_PAUSED,40);return;}
    if(d.menu) {
        if(pressed&KEY_A){den_save();d.menu=0;}
        if(pressed&KEY_B){d.menu=0;den_notice(DEN_N_RESUME,30);}
        return;
    }
    if(pressed&KEY_L){d.inventory=d.inventory?0:1;return;}
    if(d.inventory){den_inventory_input(pressed);return;}
    if((pressed&KEY_A)&&den_interact())return;
    if(held&KEY_UP) {
        if(held&KEY_RIGHT){d.aim=1;den_try_move_player(0,-DEN_MOVE_ONE);}
        else if(held&KEY_LEFT){d.aim=7;den_try_move_player(-DEN_MOVE_ONE,0);}
        else {d.aim=0;den_try_move_player(-DEN_MOVE_ONE,-DEN_MOVE_ONE);}
    } else if(held&KEY_DOWN) {
        if(held&KEY_RIGHT){d.aim=3;den_try_move_player(DEN_MOVE_ONE,0);}
        else if(held&KEY_LEFT){d.aim=5;den_try_move_player(0,DEN_MOVE_ONE);}
        else {d.aim=4;den_try_move_player(DEN_MOVE_ONE,DEN_MOVE_ONE);}
    } else if(held&KEY_RIGHT){d.aim=2;den_try_move_player(DEN_MOVE_ONE,-DEN_MOVE_ONE);}
    else if(held&KEY_LEFT){d.aim=6;den_try_move_player(-DEN_MOVE_ONE,DEN_MOVE_ONE);}
    if(held&(KEY_UP|KEY_DOWN|KEY_LEFT|KEY_RIGHT)) {
        if(d.animation!=1){d.animation=1;d.anim_tick=0;}
        else if(++d.anim_tick>=12)d.anim_tick=0;
    } else if(d.animation==1){d.animation=0;d.anim_tick=0;}
    if((pressed&KEY_R)&&d.potions){--d.potions;d.hp=(s16)den_min(d.max_hp,d.hp+25);d.mana=(s16)den_min(d.max_mana,d.mana+5);}
    else if((pressed&KEY_R)&&!d.potions)den_notice(DEN_N_NO_POTION,25);
    if(d.attack_cooldown)--d.attack_cooldown;
    if(d.bash_cooldown)--d.bash_cooldown;
    if(d.area==DEN_AREA_CAVE&&(held&KEY_A)&&!d.attack_cooldown){den_attack(0);d.attack_cooldown=12;}
    if(d.area==DEN_AREA_CAVE&&(pressed&KEY_B)&&!d.bash_cooldown){
        if(d.mana<2)den_notice(DEN_N_NO_MANA,25);
        else {d.mana-=DEN_BASH_COST;den_attack(1);d.bash_cooldown=18;d.animation=2;d.anim_tick=0;den_notice(DEN_N_BASH,18);}
    }
    if((d.tick&7)==0 && d.mana<d.max_mana) ++d.mana;
    if(d.animation==2 || d.animation==3) {if(++d.anim_tick>=8){d.animation=0;d.anim_tick=0;}}
    if(d.area==DEN_AREA_CAVE&&(d.tick&7)==0) {int y,x,cx=den_tx(d.px),cy=den_ty(d.py);
        for(y=cy-3;y<=cy+3;y++)for(x=cx-3;x<=cx+3;x++)if(den_floor(x,y))d.discovered[y][x]=1;
    }
    den_update_mobs();

}
static void den_init(void)
{
    d=(DenState){0};
}

/* Unit and control-flow fixtures only: tests drive the public controller API. */
static int den_self_test(void)
{
    int fails=0,i,points;DenItem item;
    den_start_new();
    if(d.mob_count!=37||den_live_required()!=37||d.area!=DEN_AREA_TOWN||!den_valid_gear(&d.gear))fails|=1;
    den_update(KEY_B,KEY_B);if(d.mana!=10)fails|=2; /* town is safe */
    d.px=DEN_AKARA_X*8+4;d.py=DEN_AKARA_Y*8+4;
    den_update(KEY_A,KEY_A);den_update(KEY_A,KEY_A);
    if(!d.quest_started||d.dialog!=DEN_DIALOG_ACTIVE)fails|=4;
    den_update(KEY_B,KEY_B);den_enter_area(DEN_AREA_CAVE);
    d.mana=1;den_update(0,KEY_B);if(d.mana!=1||d.notice!=DEN_N_NO_MANA)fails|=8;
    for(i=0;i<d.mob_count;i++){d.mobs[i].alive=0;d.mobs[i].corpse=1;}
    d.mobs[0].alive=1;d.mobs[0].hp=1;den_kill(&d.mobs[0]);
    if(d.quest!=DEN_QUEST_RETURN)fails|=16;
    points=d.skill_points;den_enter_area(DEN_AREA_TOWN);
    if(d.skill_points!=points||d.rewarded)fails|=32; /* exit never rewards */
    den_talk_akara();den_dialog_input(KEY_A);
    if(d.quest!=DEN_QUEST_DONE||!d.rewarded||d.skill_points!=points+1||d.screen!=DEN_PLAY)fails|=64;
    den_dialog_input(KEY_A);den_talk_akara();den_dialog_input(KEY_A);
    if(d.skill_points!=points+1)fails|=128;
    den_start_new();den_item_make(&item,DEN_ITEM_CAP,99);den_inventory_add(&d.gear,item,&i);
    den_drop_inventory(i);i=den_nearest_loot(14);if(i<0)fails|=256;else den_pickup(i);
    if(!den_valid_gear(&d.gear))fails|=512;
    den_save();d.gold=300;d.gear.grid[0]=255;if(!den_load()||d.gold==300||!den_valid_gear(&d.gear))fails|=1024;
    /* A resurrected enemy cannot produce another item/gold drop. */
    den_enter_area(DEN_AREA_CAVE);den_kill(&d.mobs[0]);points=0;
    for(i=0;i<DEN_LOOT_MAX;i++)points+=d.loot[i].active;
    d.mobs[0].alive=1;den_kill(&d.mobs[0]);
    for(i=0;i<DEN_LOOT_MAX;i++)points-=d.loot[i].active;
    if(points)fails|=2048;
    den_start_new();den_enter_area(DEN_AREA_CAVE);
    for(i=0;i<DEN_LOOT_MAX;i++){d.loot[i].active=1;d.loot[i].kind=DEN_LOOT_GOLD;d.loot[i].area=DEN_AREA_CAVE;}
    den_kill(&d.mobs[0]);if(d.loot_dropped[0])fails|=4096;
    d.loot[0].active=0;den_update_mobs();
    if(!d.loot[0].active||!d.loot_dropped[0])fails|=4096;
    return fails;
}
#endif
