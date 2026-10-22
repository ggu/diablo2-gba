#ifdef D2_DEN_OF_EVIL
#include "../common/gba.h"
#include "den_sim.h"
#include "den_display.h"
#include "d2_den_assets.h"
#include "../.local/d2/generated/d2_town_assets.h"

/* The Mac importer scales original art offline; no MPQ/DCC work runs on GBA. */
#define DEN_VIEW_Y 14
#define DEN_VIEW_H 118
#define DEN_WORLD_W 512
#define DEN_WORLD_H 256
static u8 den_world[DEN_WORLD_W*DEN_WORLD_H] __attribute__((aligned(4)));
static int den_world_valid;
static u8 den_world_area,den_world_area_serial;
static u8 den_upper_hud[240*14] __attribute__((aligned(4)));
static u8 den_lower_hud[240*28] __attribute__((aligned(4)));
static int den_upper_key[4],den_lower_key[7],den_upper_valid,den_lower_valid;
static int den_key_changed(int *previous,const int *current,int count) {
    int changed=0;
    for(int i=0;i<count;i++){if(previous[i]!=current[i])changed=1;previous[i]=current[i];}
    return changed;
}
#define DEN_SCENERY_MAX 64
static struct {s16 x,y;u8 kind;} den_scenery[DEN_SCENERY_MAX];
static int den_scenery_count;
volatile u32 den_debug[8];
volatile u32 den_terrain_cycles, den_actor_cycles;

static GBA_HOT int den_sx(int x,int y) { return 120+(x-d.px-y+d.py); }
static GBA_HOT int den_sy(int x,int y) { return 68+(x-d.px+y-d.py)/2; }

/* This fixed cave fits in 128 KiB. Build its real-art surface once, then
   copy a clipped camera window directly into Mode 4 on every frame. */
static GBA_HOT void den_prepare_world(void) {
    for(int i=0;i<DEN_WORLD_W*DEN_WORLD_H;i++)den_world[i]=C_BLACK;
    if(d.area==DEN_AREA_TOWN) {
        /* The town and cave share the same bounded cache. Each authentic 40x32
           ground tile covers a five-by-five patch of the authored walk grid. */
        for(int my=0;my<7;my++)for(int mx=0;mx<7;mx++) {
            int sx=256+(mx-my)*20-20,sy=(mx+my)*10;
            unsigned variant=(unsigned)(mx*11+my*17+(mx*my)%7)%d2_town_ground_tile_count;
            const D2TownTile *tile=&d2_town_ground_tiles[variant];
            for(int yy=0;yy<tile->h;yy++) {
                int py=sy+yy;if((unsigned)py>=DEN_WORLD_H)continue;
                for(int xx=0;xx<tile->w;xx++) {
                    int px=sx+xx;if((unsigned)px>=DEN_WORLD_W)continue;
                    u8 col=tile->pixels[yy*tile->w+xx];if(!col)continue;
                    int lx=(xx-20+2*yy)/8,ly=(2*yy-xx+20)/8;
                    int tx=mx*5+lx,ty=my*5+ly;
                    if(tx>=0&&ty>=0&&tx<DEN_TOWN_W&&ty<DEN_TOWN_H&&den_town_floor(tx,ty))
                        den_world[py*DEN_WORLD_W+px]=col;
                }
            }
        }
        den_scenery_count=0;den_world_valid=1;
        den_world_area=d.area;den_world_area_serial=d.area_serial;
        return;
    }
    for(int my=0;my<13;my++)for(int mx=0;mx<13;mx++) {
        int sx=256+(mx-my)*20-20,sy=(mx+my)*10;
        unsigned variant=(unsigned)(mx*17+my*31)%d2_den_floor_count;
        const u8 *src=d2_den_floors[variant];
        for(int yy=0;yy<20;yy++) {
            int py=sy+yy;if((unsigned)py>=DEN_WORLD_H)continue;
            for(int xx=0;xx<40;xx++) {
                int px=sx+xx;if((unsigned)px>=DEN_WORLD_W)continue;
                u8 col=src[yy*40+xx];if(!col)continue;
                int lx=(xx-20+2*yy)/8,ly=(2*yy-xx+20)/8;
                int tx=mx*5+lx,ty=my*5+ly;
                if((unsigned)tx<DEN_MAP_W&&(unsigned)ty<DEN_MAP_H&&d.map[ty][tx])
                    den_world[py*DEN_WORLD_W+px]=col;
            }
        }
    }
    den_scenery_count=0;
    for(int y=2;y<DEN_MAP_H-1;y+=4)for(int x=2;x<DEN_MAP_W-1;x+=4) {
        if(d.map[y][x]||((x+y)/4)&1)continue;
        if(!d.map[y][x-1]&&!d.map[y][x+1]&&!d.map[y-1][x]&&!d.map[y+1][x])continue;
        if(den_scenery_count>=DEN_SCENERY_MAX)continue;
        den_scenery[den_scenery_count].x=x*8+4;
        den_scenery[den_scenery_count].y=y*8+4;
        den_scenery[den_scenery_count].kind=(x*13+y*7)%d2_den_rock_count;
        ++den_scenery_count;
    }
    den_world_valid=1;
    den_world_area=d.area;den_world_area_serial=d.area_serial;
}
static GBA_HOT void den_frame(const D2SpriteFrame *f,int x,int y) {
    gfx_blit_sprite_2x(x+f->x*2,y+f->y*2,f->w,f->h,f->pixels);
}
static GBA_HOT void den_actor(int kind,int animation,int dir,int tick,int x,int y) {
    if((unsigned)animation>4)animation=0;
    const D2Animation *a=&d2_den_actors[kind][animation];
    if(!a->frames||!a->dirs)return;
    int frame;
    if(animation==0)frame=(d.tick/5)%a->frames;
    else if(animation==1)frame=(tick%12)*a->frames/12;
    else if(animation==4)frame=clampi(tick/2,0,a->frames-1);
    else frame=clampi(tick,0,7)*a->frames/8;
    dir=((unsigned)dir%8)*a->dirs/8;
    den_frame(&a->images[dir*a->frames+frame],x,y);
}
static GBA_HOT void den_animation(const D2Animation *a,int dir,int tick,int x,int y) {
    if(!a||!a->frames||!a->dirs)return;
    int frame=(tick/5)%a->frames;dir=((unsigned)dir%8)*a->dirs/8;
    den_frame(&a->images[dir*a->frames+frame],x,y);
}
static void den_town_tent(int tx,int ty,int rx) {
    int x=den_sx(tx*8+4,ty*8+4),y=den_sy(tx*8+4,ty*8+4);
    gfx_diamond(x,y,rx*2,10,C_SHADOW);gfx_diamond(x,y-8,(rx-1)*2,10,C_BROWN);
    gfx_line(x-(rx-2)*2,y-6,x,y-22,C_GOLD_DARK);gfx_line(x,y-22,x+(rx-2)*2,y-6,C_GOLD_DARK);
    gfx_rect(x-4,y-6,9,10,C_BLACK);gfx_line(x-2,y-4,x+2,y-4,C_BROWN);
}
static void den_town_campfire(void) {
    int x=den_sx(18*8+4,18*8+4),y=den_sy(18*8+4,18*8+4);
    gfx_diamond(x,y,12,4,C_SHADOW);den_animation(&d2_town_campfire,0,d.tick,x,y);
}
static void den_draw_orb(int x,int value,int max,u8 color) {
    gfx_circle(x,145,11,C_EDGE);gfx_circle(x,145,9,C_RED_DARK);
    int fill=max>0?clampi(value,0,max)*18/max:0;
    for(int yy=-9;yy<=9;yy++) {
        int span=9;while(span*span+yy*yy>81)span--;
        gfx_rect(x-span,145+yy,2*span+1,1,yy>=9-fill?color:C_PANEL);
    }
}
static void den_minimap(void) {
    gfx_rect(173,18,65,65,C_BLACK);gfx_frame(173,18,65,65,C_GOLD_DARK);
    if(d.area==DEN_AREA_TOWN) {
        for(int y=0;y<DEN_TOWN_H;y++)for(int x=0;x<DEN_TOWN_W;x++)
            if(den_town_floor(x,y))gfx_pixel(174+x,19+y,C_STONE);
        gfx_pixel(174+DEN_GATE_X,19+DEN_GATE_Y,C_GOLD);
        gfx_pixel(174+DEN_AKARA_X,19+DEN_AKARA_Y,C_CYAN);
    } else {
        for(int y=0;y<DEN_MAP_H;y++)for(int x=0;x<DEN_MAP_W;x++) {
            if(d.map[y][x]&&d.discovered[y][x])gfx_pixel(174+x,19+y,d.map[y][x]==2?C_GOLD:C_STONE);
        }
        for(int i=0;i<d.mob_count;i++)if(d.mobs[i].alive&&d.discovered[d.mobs[i].y/8][d.mobs[i].x/8])
            gfx_pixel(174+d.mobs[i].x/8,19+d.mobs[i].y/8,d.mobs[i].elite?C_GOLD:C_RED);
    }
    gfx_pixel(174+d.px/8,19+d.py/8,C_WHITE);
}
static const char *den_slot_name(int slot) {
    static const char *names[DEN_EQUIP_SLOTS]={"WEAPON","SHIELD","HEAD","TORSO","GLOVES","BELT","BOOTS"};
    return (unsigned)slot<DEN_EQUIP_SLOTS?names[slot]:"SLOT";
}
static const char *den_slot_short(int slot) {
    static const char *names[DEN_EQUIP_SLOTS]={"WPN","SHLD","HEAD","BODY","GLOVE","BELT","BOOT"};
    return (unsigned)slot<DEN_EQUIP_SLOTS?names[slot]:"SLOT";
}
static GBA_HOT void den_item_icon(const D2SpriteFrame *f,int x,int y,int w,int h) {
    if(!f||!f->pixels||!f->w||!f->h||w<1||h<1)return;
    u8 map_x[32],map_y[40];if(w>32||h>40)return;
    for(int px=0;px<w;px++)map_x[px]=(u8)(px*f->w/w);
    for(int py=0;py<h;py++)map_y[py]=(u8)(py*f->h/h);
    for(int py=0;py<h;py++)for(int px=0;px<w;px++) {
        u8 color=f->pixels[map_y[py]*f->w+map_x[px]];
        if(color)gfx_pixel(x+px,y+py,color);
    }
}
static void den_item_detail(int record,int x,int y) {
    if((unsigned)record>=DEN_ITEM_RECORDS||!d.gear.items[record].base_id) {
        gfx_text(x,y,"EMPTY",C_DIM);return;
    }
    const DenItem *item=&d.gear.items[record];
    const DenItemDef *def=den_item_def(item->base_id);if(!def)return;
    gfx_text(x,y,def->name,C_GOLD);
    if(def->category==DEN_ITEM_WEAPON) {
        gfx_text(x,y+9,"DAMAGE",C_DIM);gfx_num(x+45,y+9,def->min_damage,C_WHITE);
        gfx_text(x+55,y+9,"-",C_DIM);gfx_num(x+63,y+9,def->max_damage,C_WHITE);
    } else {
        gfx_text(x,y+9,"DEFENSE",C_DIM);gfx_num(x+48,y+9,item->defense_roll,C_WHITE);
    }
    gfx_text(x,y+18,"LV",C_DIM);gfx_num(x+13,y+18,def->req_level,C_WHITE);
    gfx_text(x+25,y+18,"STR",C_DIM);gfx_num(x+48,y+18,def->req_str,C_WHITE);
    gfx_text(x,y+27,"DEX",C_DIM);gfx_num(x+28,y+27,def->req_dex,C_WHITE);
    gfx_text(x,y+36,"DURA",C_DIM);gfx_num(x+31,y+36,item->durability,C_WHITE);
    gfx_text(x+49,y+36,"/",C_DIM);gfx_num(x+57,y+36,def->max_durability,C_WHITE);
    if(!den_item_requirements(item,d.str,d.dex,d.level))gfx_text(x,y+45,"REQ NOT MET",C_RED);
}
static void den_inventory_grid(void) {
    int cx=d.inv_cursor%DEN_INVENTORY_W,cy=d.inv_cursor/DEN_INVENTORY_W;
    gfx_rect(10,18,220,111,C_BLACK);gfx_frame(10,18,220,111,C_GOLD_DARK);
    gfx_text(17,22,"INVENTORY",C_GOLD);gfx_text(98,22,"EQUIPMENT",C_DIM);gfx_text(177,22,"CHARACTER",C_DIM);
    gfx_line(16,34,224,34,C_GOLD_DARK);
    for(int i=0;i<DEN_INVENTORY_CELLS;i++) {
        int x=17+(i%DEN_INVENTORY_W)*12,y=39+(i/DEN_INVENTORY_W)*13;
        gfx_rect(x,y,11,12,C_PANEL);gfx_frame(x,y,11,12,C_EDGE);
    }
    for(int i=0;i<DEN_INVENTORY_CELLS;i++) {
        int ref=d.gear.grid[i];if(!ref||ref>DEN_ITEM_RECORDS)continue;
        int first=1;for(int j=0;j<i;j++)if(d.gear.grid[j]==ref){first=0;break;}
        if(!first)continue;
        const DenItemDef *def=den_item_def(d.gear.items[ref-1].base_id);if(!def)continue;
        int x=18+(i%DEN_INVENTORY_W)*12,y=40+(i/DEN_INVENTORY_W)*13;
        den_item_icon(&d2_item_icons[def->icon_id],x,y,def->width*12-2,def->height*13-2);
    }
    gfx_frame(17+(d.inv_cursor%DEN_INVENTORY_W)*12,39+(d.inv_cursor/DEN_INVENTORY_W)*13,11,12,C_GOLD);
    gfx_rect(144,38,78,70,C_PANEL);gfx_frame(144,38,78,70,C_EDGE);
    gfx_text(150,42,"ITEM",C_DIM);
    int ref=d.gear.grid[cy*DEN_INVENTORY_W+cx];
    if(ref&&ref<=DEN_ITEM_RECORDS)den_item_detail(ref-1,150,54);else gfx_text(150,57,"EMPTY",C_DIM);
    gfx_text(17,95,"A EQUIP",C_CYAN);gfx_text(17,107,"B DROP",C_CYAN);
    gfx_num(76,107,d.potions,C_GOLD);gfx_text(89,107,"POTIONS",C_DIM);
    gfx_text(151,115,"SEL NEXT TAB",C_GOLD);
}
static void den_equipment_panel(void) {
    gfx_rect(10,18,220,111,C_BLACK);gfx_frame(10,18,220,111,C_GOLD_DARK);
    gfx_text(17,22,"INVENTORY",C_DIM);gfx_text(98,22,"EQUIPMENT",C_GOLD);gfx_text(177,22,"CHARACTER",C_DIM);
    gfx_line(16,34,224,34,C_GOLD_DARK);
    for(int slot=0;slot<DEN_EQUIP_SLOTS;slot++) {
        int x=18+(slot%2)*61,y=40+(slot/2)*20;
        if(slot==DEN_SLOT_BOOTS)x=48;
        int selected=slot==d.equip_cursor;gfx_rect(x,y,56,16,selected?C_HIGHLIGHT:C_PANEL);gfx_frame(x,y,56,16,selected?C_GOLD:C_EDGE);
        gfx_text(x+2,y+4,den_slot_short(slot),C_DIM);
        int ref=d.gear.equipment[slot];if(ref&&ref<=DEN_ITEM_RECORDS) {
            const DenItemDef *def=den_item_def(d.gear.items[ref-1].base_id);
            if(def)den_item_icon(&d2_item_icons[def->icon_id],x+39,y+1,14,14);
        }
    }
    gfx_rect(144,39,78,68,C_PANEL);gfx_frame(144,39,78,68,C_EDGE);
    gfx_text(150,43,den_slot_name(d.equip_cursor),C_GOLD);
    int ref=d.gear.equipment[d.equip_cursor];if(ref&&ref<=DEN_ITEM_RECORDS)den_item_detail(ref-1,150,55);else gfx_text(150,59,"EMPTY",C_DIM);
    gfx_text(17,107,"A UNEQUIP",C_CYAN);gfx_text(99,107,"SEL NEXT TAB",C_GOLD);
}
static void den_character_panel(void) {
    static const char *stats[5]={"STRENGTH","DEXTERITY","VITALITY","ENERGY","BASH"};
    int vals[5]={d.str,d.dex,d.vit,d.energy,d.bash_rank};
    DenItemStats equip;den_item_stats(&d.gear,d.str,d.dex,d.level,&equip);
    gfx_rect(10,18,220,111,C_BLACK);gfx_frame(10,18,220,111,C_GOLD_DARK);
    gfx_text(17,22,"INVENTORY",C_DIM);gfx_text(98,22,"EQUIPMENT",C_DIM);gfx_text(177,22,"CHARACTER",C_GOLD);
    gfx_line(16,34,224,34,C_GOLD_DARK);
    gfx_text(18,40,"BARBARIAN",C_GOLD);gfx_text(18,54,"LEVEL",C_DIM);gfx_num(55,54,d.level,C_WHITE);
    gfx_text(18,67,"LIFE",C_RED);gfx_num(53,67,d.hp,C_WHITE);gfx_text(68,67,"/",C_DIM);gfx_num(76,67,d.max_hp,C_WHITE);
    gfx_text(18,80,"MANA",C_CYAN);gfx_num(53,80,d.mana,C_WHITE);gfx_text(68,80,"/",C_DIM);gfx_num(76,80,d.max_mana,C_WHITE);
    gfx_text(18,95,"DAMAGE",C_DIM);gfx_num(69,95,equip.min_damage,C_WHITE);gfx_text(83,95,"-",C_DIM);gfx_num(91,95,equip.max_damage,C_WHITE);
    gfx_text(18,108,"DEF",C_DIM);gfx_num(45,108,equip.defense+d.dex/4,C_WHITE);gfx_text(73,108,"BLOCK",C_CYAN);gfx_num(109,108,equip.block_chance,C_WHITE);
    gfx_rect(137,39,85,68,C_PANEL);gfx_frame(137,39,85,68,C_EDGE);
    gfx_text(143,42,"STAT PTS",C_GOLD);gfx_num(197,42,d.stat_points,C_CYAN);
    for(int i=0;i<5;i++){int y=53+i*10;gfx_text(143,y,stats[i],i==d.stat_cursor?C_WHITE:C_DIM);gfx_num(202,y,vals[i],C_WHITE);}
    gfx_text(17,119,"SKILL PTS",C_DIM);gfx_num(72,119,d.skill_points,C_WHITE);
    gfx_text(131,119,"A SPEND  SEL TAB",C_GOLD);
}
static void den_inventory(void) {
    if(d.inventory==1)den_inventory_grid();else if(d.inventory==2)den_equipment_panel();else den_character_panel();
}
void den_debug_checkpoint(void);
void game_init(void) {
    den_init();den_world_valid=0;den_world_area_serial=255;den_upper_valid=den_lower_valid=0;
    den_display_palette(d2_den_palette);den_debug_checkpoint();
}
void den_debug_checkpoint(void) {
    den_debug[0]=den_state_hash();den_debug[1]=d.screen;den_debug[2]=d.hp;
    den_debug[3]=d.quest;den_debug[4]=d.kills;den_debug[5]=d.px;
    den_debug[6]=d.py;den_debug[7]=d.skill_points;
}
void game_update(u16 held,u16 pressed) {
    int previous_screen=d.screen;
    u8 previous_area_serial=d.area_serial;
    den_update(held,pressed);
    if(previous_screen!=DEN_PLAY&&d.screen==DEN_PLAY){den_world_valid=0;den_upper_valid=den_lower_valid=0;}
    if(previous_area_serial!=d.area_serial){den_world_valid=0;den_upper_valid=den_lower_valid=0;}
}
int game_self_test(void) { return den_self_test(); }
GBA_HOT void game_render(void) {
    gfx_begin(C_BLACK);
    if(d.screen==DEN_TITLE) {
        gfx_text(88,19,"DIABLO II",C_GOLD);gfx_text(77,34,"DEN OF EVIL",C_RED);
        den_actor(0,0,0,d.tick,120,91);
        gfx_text(92,100,"BARBARIAN",C_GOLD);
        gfx_text(40,119,"A NEW   SELECT LOAD",C_WHITE);
        gfx_text(31,135,"ORIGINAL ART - GBA TEST",C_DIM);
        gfx_text(40,148,"ONE FIXED CAVE LAYOUT",C_DIM);
        gfx_end();return;
    }
    if(d.screen==DEN_DEAD||d.screen==DEN_WIN) {
        gfx_text(d.screen==DEN_DEAD?78:66,35,d.screen==DEN_DEAD?"YOU HAVE DIED":"DEN OF EVIL CLEAR",d.screen==DEN_DEAD?C_RED:C_GOLD);
        if(d.screen==DEN_DEAD)den_actor(0,4,d.aim,d.anim_tick,120,90);
        if(d.screen==DEN_WIN) {
            gfx_text(32,58,"AKARA REWARDS YOUR COURAGE",C_WHITE);
            gfx_text(62,73,"+1 SKILL POINT",C_CYAN);
            gfx_text(60,88,"QUEST COMPLETE",C_GOLD);
        }
        gfx_text(42,114,"A RESTART SELECT TITLE",C_WHITE);
        gfx_text(67,135,"LIFE",C_DIM);gfx_num(100,135,d.hp,C_WHITE);
        gfx_text(131,135,"KILLS",C_DIM);gfx_num(170,135,d.kills,C_WHITE);
        gfx_end();return;
    }
    u32 stamp=gba_clock();
    if(!den_world_valid||den_world_area!=d.area||den_world_area_serial!=d.area_serial)den_prepare_world();
    gfx_blit_window_2x(DEN_VIEW_Y,DEN_VIEW_H,den_world,DEN_WORLD_W,DEN_WORLD_H,
                    256+(d.px-d.py)/2-60,(d.px+d.py)/4-27);
    den_terrain_cycles=gba_clock()-stamp;stamp=gba_clock();
    int order[DEN_MOB_MAX+DEN_SCENERY_MAX+DEN_LOOT_MAX+2],depth[DEN_MOB_MAX+DEN_SCENERY_MAX+DEN_LOOT_MAX+2],count=0;
    s16 mob_x[DEN_MOB_MAX],mob_y[DEN_MOB_MAX];
    s16 scenery_x[DEN_SCENERY_MAX],scenery_y[DEN_SCENERY_MAX];
    s16 loot_x[DEN_LOOT_MAX],loot_y[DEN_LOOT_MAX];
    const int player_tag=DEN_MOB_MAX,scenery_tag=DEN_MOB_MAX+1;
    const int akara_tag=DEN_MOB_MAX+1+DEN_SCENERY_MAX,loot_tag=akara_tag+1;
    for(int i=0;i<DEN_MOB_MAX;i++)if(d.area==DEN_AREA_CAVE&&(d.mobs[i].alive||d.mobs[i].corpse)) {
        int x=den_sx(d.mobs[i].x,d.mobs[i].y),y=den_sy(d.mobs[i].x,d.mobs[i].y);
        if(x< -64||x>304||y< -64||y>196)continue;
        mob_x[i]=(s16)x;mob_y[i]=(s16)y;
        order[count]=i;depth[count++]=d.mobs[i].x+d.mobs[i].y;
    }
    order[count]=player_tag;depth[count++]=d.px+d.py;
    for(int i=0;i<den_scenery_count;i++) {
        int x=den_sx(den_scenery[i].x,den_scenery[i].y),y=den_sy(den_scenery[i].x,den_scenery[i].y);
        if(x< -80||x>320||y< -64||y>=200)continue;
        scenery_x[i]=(s16)x;scenery_y[i]=(s16)y;
        order[count]=scenery_tag+i;
        depth[count++]=den_scenery[i].x+den_scenery[i].y;
    }
    int akara_x=0,akara_y=0;
    if(d.area==DEN_AREA_TOWN) {
        akara_x=den_sx(DEN_AKARA_X*8+4,DEN_AKARA_Y*8+4);akara_y=den_sy(DEN_AKARA_X*8+4,DEN_AKARA_Y*8+4);
        if(akara_x>=-64&&akara_x<=304&&akara_y>=-64&&akara_y<=196){order[count]=akara_tag;depth[count++]=DEN_AKARA_X*8+DEN_AKARA_Y*8+8;}
    }
    for(int i=0;i<DEN_LOOT_MAX;i++)if(d.loot[i].active&&d.loot[i].area==d.area) {
        int x=den_sx(d.loot[i].x,d.loot[i].y),y=den_sy(d.loot[i].x,d.loot[i].y);
        if(x< -60||x>300||y< -40||y>190)continue;
        loot_x[i]=(s16)x;loot_y[i]=(s16)y;
        order[count]=loot_tag+i;depth[count++]=d.loot[i].x+d.loot[i].y;
    }
    for(int i=1;i<count;i++) {
        int o=order[i],z=depth[i],j=i;
        while(j&&depth[j-1]>z){order[j]=order[j-1];depth[j]=depth[j-1];j--;}
        order[j]=o;depth[j]=z;
    }
    for(int n=0;n<count;n++) {
        int i=order[n];
        if(i==player_tag) {
            gfx_diamond(120,68,13,5,C_BLACK);
            gfx_diamond(120,68,11,4,C_HIGHLIGHT);
            gfx_diamond(120,68,9,3,C_SHADOW);
            den_actor(0,d.animation,d.aim,d.anim_tick,120,68);
        }
        else if(i>=scenery_tag&&i<akara_tag) {
            int rock=i-scenery_tag;
            den_frame(&d2_den_rocks[den_scenery[rock].kind],scenery_x[rock],scenery_y[rock]);
        }
        else if(i==akara_tag) {
            gfx_diamond(akara_x,akara_y,4,1,C_SHADOW);den_animation(&d2_town_akara_neutral,0,d.tick,akara_x,akara_y);
        }
        else if(i>=loot_tag) {
            int li=i-loot_tag;DenGroundItem *g=&d.loot[li];
            u8 color=g->kind==DEN_LOOT_GOLD?C_GOLD:g->kind==DEN_LOOT_POTION?C_RED:C_CYAN;
            gfx_diamond(loot_x[li],loot_y[li],3,2,color);
        } else {
            DenMob *m=&d.mobs[i];int x=mob_x[i],y=mob_y[i];
            if(m->alive)gfx_diamond(x,y,8,2,C_SHADOW);
            den_actor(1+m->kind,m->alive?m->anim:4,m->dir,m->anim_tick,x,y);
            if(m->elite&&m->alive) {
                gfx_text(x-30,y-51,"CORPSEFIRE",C_GOLD);
                gfx_bar(x-24,y-40,48,3,m->hp,m->max_hp,C_RED,C_BLACK);
            } else if(m->flash&&m->alive)gfx_bar(x-14,y-28,28,3,m->hp,m->max_hp,C_RED,C_BLACK);
        }
    }
    int nearest=den_nearest_loot(14);
    if(d.area==DEN_AREA_TOWN){den_town_tent(6,8,8);den_town_tent(21,9,12);den_town_tent(7,25,9);den_town_campfire();}
    for(int i=0;i<DEN_LOOT_MAX;i++)if(d.loot[i].active&&d.loot[i].area==d.area) {
        DenGroundItem *g=&d.loot[i];int x=den_sx(g->x,g->y),y=den_sy(g->x,g->y);
        if(x< -60||x>300||y< -40||y>190)continue;
        if(g->kind==DEN_LOOT_ITEM) {
            const DenItemDef *def=den_item_def(g->item.base_id);
            if(def)den_frame(&d2_item_ground_icons[def->icon_id],x,y);
        } else {
            u8 color=g->kind==DEN_LOOT_GOLD?C_GOLD:C_RED;gfx_diamond(x,y,6,4,color);
        }
        if(i==nearest) {
            gfx_frame(x-10,y-12,21,14,C_WHITE);
            if(g->kind==DEN_LOOT_GOLD){gfx_text(x-16,y-25,"GOLD",C_GOLD);gfx_num(x+9,y-25,g->amount,C_WHITE);}
            else if(g->kind==DEN_LOOT_POTION)gfx_text(x-17,y-25,"POTION",C_RED);
            else {const DenItemDef *def=den_item_def(g->item.base_id);if(def)gfx_text(x-20,y-25,def->name,C_CYAN);}
        }
    }
    den_actor_cycles=gba_clock()-stamp;
    int left=0;for(int i=0;i<DEN_MOB_MAX;i++)left+=!!d.mobs[i].alive;
    int near_akara=den_near_akara();
    int near_gate=d.area==DEN_AREA_TOWN&&den_distance(d.px,d.py,DEN_GATE_X*8+4,DEN_GATE_Y*8+4)<=16;
    int near_exit=d.area==DEN_AREA_CAVE&&den_distance(d.px,d.py,DEN_ENTRY_X*8+4,DEN_ENTRY_Y*8+4)<=12;
    int hint=d.area==DEN_AREA_TOWN?(near_akara?1:near_gate?2:nearest>=0?3:0):
             (near_exit?2:nearest>=0?3:0);
    const char *action=d.area==DEN_AREA_TOWN?(hint==1?"A TALK TO AKARA":hint==2?"A ENTER DEN":hint==3?"A PICK ITEM":"A INTERACT"):
                       (hint==2?"A RETURN TO CAMP":hint==3?"A PICK ITEM":"A ATTACK");
    int upper_key[]={left,d.level,d.gold,d.area};
    int lower_key[]={d.hp,d.max_hp,d.mana,d.max_mana,d.potions,d.area,hint};
    int upper_changed=den_key_changed(den_upper_key,upper_key,4);
    int lower_changed=den_key_changed(den_lower_key,lower_key,7);
    if(!den_upper_valid||upper_changed) {
    gfx_rect(0,0,240,14,C_BLACK);gfx_text(3,3,d.area==DEN_AREA_TOWN?"ROGUE CAMP":"DEN OF EVIL",C_GOLD);
    if(d.area==DEN_AREA_CAVE){gfx_text(81,3,"LEFT",C_DIM);gfx_num(111,3,left,C_WHITE);}
    gfx_text(149,3,"LV",C_DIM);gfx_num(166,3,d.level,C_WHITE);
    gfx_text(201,3,"G",C_GOLD);gfx_num(211,3,d.gold,C_WHITE);
        gfx_read_band(0,14,den_upper_hud);den_upper_valid=1;
    } else gfx_blit_band(0,14,den_upper_hud);
    if(!den_lower_valid||lower_changed) {
    gfx_rect(0,132,240,28,C_BLACK);gfx_frame(0,132,240,28,C_GOLD_DARK);
    den_draw_orb(13,d.hp,d.max_hp,C_RED);den_draw_orb(226,d.mana,d.max_mana,C_BLUE);
    gfx_num(29,138,d.hp,C_WHITE);gfx_text(56,138,action,C_WHITE);
    gfx_num(195,138,d.mana,C_CYAN);
    gfx_text(29,150,"R POT",C_DIM);gfx_num(64,150,d.potions,C_GOLD);
    gfx_text(86,150,"L GEAR SEL MAP",C_DIM);
        gfx_read_band(132,28,den_lower_hud);den_lower_valid=1;
    } else gfx_blit_band(132,28,den_lower_hud);
    if(d.quest==DEN_QUEST_RETURN) {
        if(d.area==DEN_AREA_CAVE) {
            gfx_rect(25,114,190,13,C_PANEL);gfx_text(34,117,"RETURN THROUGH THE CAMP GATE",C_GOLD);
            int x=den_sx(DEN_ENTRY_X*8+4,DEN_ENTRY_Y*8+4),y=den_sy(DEN_ENTRY_X*8+4,DEN_ENTRY_Y*8+4);
            gfx_frame(x-4,y-5,9,7,C_GOLD);
        } else {gfx_rect(25,114,190,13,C_PANEL);gfx_text(34,117,"RETURN TO AKARA FOR YOUR REWARD",C_GOLD);}
    }
    if(d.notice_time) {
        const char *text=den_notice_text();
        if(d.notice==DEN_N_RETURN)text="DEN CLEAR! RETURN THROUGH CAMP GATE";
        gfx_rect(22,18,196,11,C_BLACK);gfx_text(27,20,text,C_CYAN);
    }
    if(d.automap)den_minimap();
    if(d.inventory)den_inventory();
    if(d.dialog) {
        gfx_rect(22,84,196,45,C_BLACK);gfx_frame(22,84,196,45,C_GOLD);
        gfx_text(29,88,"AKARA",C_GOLD);
        const char *line1="",*line2="";
        switch(d.dialog) {
        case DEN_DIALOG_OFFER: line1="The Den threatens our camp.";line2="Clear every monster within.";break;
        case DEN_DIALOG_ACTIVE: line1="The Den lies beyond the gate.";line2="Return when it is cleansed.";break;
        case DEN_DIALOG_REWARD: line1="You have cleansed the Den.";line2="Take your skill reward.";break;
        case DEN_DIALOG_THANKS: line1="You have my gratitude.";line2="Your skill has improved.";break;
        }
        gfx_text(29,99,line1,C_WHITE);gfx_text(29,109,line2,C_WHITE);
        gfx_text(29,119,d.dialog==DEN_DIALOG_OFFER||d.dialog==DEN_DIALOG_REWARD?"A ACCEPT / CLAIM": "A CONTINUE",C_CYAN);
        gfx_text(145,119,"B CLOSE",C_GOLD);
    }
    if(d.menu) {
        gfx_rect(43,51,154,61,C_BLACK);gfx_frame(43,51,154,61,C_GOLD);
        gfx_text(89,59,"PAUSED",C_GOLD);
        gfx_text(59,77,"A SAVE AND RESUME",C_WHITE);
        gfx_text(59,93,"B RESUME",C_CYAN);
    }
    gfx_end();
}
#endif
