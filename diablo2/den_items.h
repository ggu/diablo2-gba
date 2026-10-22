#ifndef D2_DEN_ITEMS_H
#define D2_DEN_ITEMS_H

#include "../common/gba.h"

/* Rules adapted from D2MOO 98a3cc281a6a96708e881718934b8e4c99bbe9e2:
 * D2Common/src/D2Inventory.cpp:764 (bounded grid placement and occupancy),
 * D2Common/src/Items/Items.cpp:5408-5465 (base weapon damage/durability),
 * D2Common/src/Units/Units.cpp:2398-2430 (Barbarian base attack rating),
 * D2Game/src/UNIT/SUnitDmg.cpp:2540-2567 (AR/defense and level hit chance),
 * D2Common/src/Units/Units.cpp:2434-2480 (class/shield block factor).
 * This slice supports only normal, non-magical base items; no affix logic.
 */

#define DEN_INVENTORY_W 10
#define DEN_INVENTORY_H 4
#define DEN_INVENTORY_CELLS 40
#define DEN_ITEM_RECORDS 48
#define DEN_EQUIP_SLOTS 7

enum { DEN_ITEM_WEAPON = 1, DEN_ITEM_SHIELD = 2, DEN_ITEM_ARMOR = 3 };
enum { DEN_SLOT_WEAPON, DEN_SLOT_SHIELD, DEN_SLOT_HEAD, DEN_SLOT_TORSO,
       DEN_SLOT_GLOVES, DEN_SLOT_BELT, DEN_SLOT_BOOTS };
enum { DEN_ITEM_QUALITY_INFERIOR = 1, DEN_ITEM_QUALITY_NORMAL = 2,
       DEN_ITEM_QUALITY_SUPERIOR = 3 };

typedef struct {
    u8 id;
    const char *name;
    const char *code;
    u8 category, width, height;
    s16 min_damage, max_damage, min_defense, max_defense;
    u8 req_str, req_dex, req_level, max_durability, block, icon_id, equip_slot;
} DenItemDef;

typedef struct {
    u8 base_id, quality, durability, flags;
    u16 defense_roll, seed;
} DenItem;

typedef struct {
    DenItem items[DEN_ITEM_RECORDS];
    u8 grid[DEN_INVENTORY_CELLS]; /* zero empty; otherwise record index + 1 */
    u8 equipment[DEN_EQUIP_SLOTS]; /* zero empty; otherwise record index + 1 */
} DenItemState;

typedef struct {
    s16 min_damage, max_damage, defense;
    u16 attack_rating, block_chance;
    u8 weapon_id, shield_id;
} DenItemStats;

#ifdef DEN_ITEMS_TEST_FIXTURES
enum { DEN_ITEM_NONE = 0, DEN_ITEM_HAND_AXE = 1, DEN_ITEM_BUCKLER = 2,
       DEN_ITEM_CAP = 3, DEN_ITEM_QUILTED_ARMOR = 4, DEN_ITEM_LEATHER_BOOTS = 5,
       DEN_ITEM_LEATHER_GLOVES = 6, DEN_ITEM_SASH = 7, DEN_ITEM_CLUB = 8,
       DEN_ITEM_SCIMITAR = 9 };
#define DEN_BARB_BLOCK_FACTOR 25
#define DEN_BARB_TO_HIT_FACTOR 120
/* Synthetic definitions keep logic tests independent of private demo assets. */
static const DenItemDef den_item_defs[] = {
    {1,"Hand Axe","hax",DEN_ITEM_WEAPON,1,3,3,6,0,0,0,0,0,28,0,1,DEN_SLOT_WEAPON},
    {2,"Buckler","buc",DEN_ITEM_SHIELD,2,2,0,0,4,6,12,0,0,12,0,2,DEN_SLOT_SHIELD},
    {3,"Cap/hat","cap",DEN_ITEM_ARMOR,2,2,0,0,3,5,0,0,0,12,0,3,DEN_SLOT_HEAD},
    {4,"Quilted Armor","qui",DEN_ITEM_ARMOR,2,3,0,0,8,11,12,0,0,20,0,4,DEN_SLOT_TORSO},
    {5,"Leather Boots","lbt",DEN_ITEM_ARMOR,2,2,0,0,2,3,0,0,0,12,0,5,DEN_SLOT_BOOTS},
    {6,"Gloves(L)","lgl",DEN_ITEM_ARMOR,2,2,0,0,2,3,0,0,0,12,0,6,DEN_SLOT_GLOVES},
    {7,"Sash(L)","lbl",DEN_ITEM_ARMOR,2,1,0,0,3,5,0,0,0,12,0,7,DEN_SLOT_BELT},
    {8,"Club","clb",DEN_ITEM_WEAPON,1,3,1,6,0,0,0,0,0,24,0,8,DEN_SLOT_WEAPON},
    {9,"Scimitar","scm",DEN_ITEM_WEAPON,1,3,2,6,0,0,0,21,0,22,0,9,DEN_SLOT_WEAPON},
};
#define DEN_ITEM_DEF_COUNT 9
#else
#include "../.local/d2/generated/d2_den_items.h"
#endif

static inline const DenItemDef *den_item_def(u8 id)
{
    int i;
    for (i=0; i<DEN_ITEM_DEF_COUNT; ++i)
        if (den_item_defs[i].id == id) return &den_item_defs[i];
    return 0;
}
static inline int den_item_clamp(int v,int lo,int hi){return v<lo?lo:v>hi?hi:v;}

static inline void den_items_reset(DenItemState *s)
{ if (s) *s=(DenItemState){0}; }

static inline int den_item_is_equipped(const DenItemState *s, int index)
{
    int i;
    if (!s || index<0 || index>=DEN_ITEM_RECORDS) return 0;
    for(i=0;i<DEN_EQUIP_SLOTS;i++) if(s->equipment[i]==index+1)return 1;
    return 0;
}

static inline int den_item_find_space(const DenItemState *s, const DenItemDef *def, int *at)
{
    int x,y,xx,yy;
    if(!s||!def||!at||def->width<1||def->height<1||def->width>DEN_INVENTORY_W||def->height>DEN_INVENTORY_H)return 0;
    for(y=0;y<=DEN_INVENTORY_H-def->height;y++)for(x=0;x<=DEN_INVENTORY_W-def->width;x++) {
        int clear=1;
        for(yy=0;yy<def->height&&clear;yy++)for(xx=0;xx<def->width;xx++)
            if(s->grid[(y+yy)*DEN_INVENTORY_W+x+xx]){clear=0;break;}
        if(clear){*at=y*DEN_INVENTORY_W+x;return 1;}
    }
    return 0;
}

static inline void den_item_stamp(DenItemState *s, int index, int at)
{
    const DenItemDef *def=den_item_def(s->items[index].base_id);
    int x=at%DEN_INVENTORY_W,y=at/DEN_INVENTORY_W,xx,yy;
    for(yy=0;yy<def->height;yy++)for(xx=0;xx<def->width;xx++)
        s->grid[(y+yy)*DEN_INVENTORY_W+x+xx]=(u8)(index+1);
}

static inline void den_item_unstamp(DenItemState *s, int index)
{
    int i;
    for(i=0;i<DEN_INVENTORY_CELLS;i++)if(s->grid[i]==index+1)s->grid[i]=0;
}

static inline int den_inventory_add(DenItemState *s, DenItem item, int *out_index)
{
    const DenItemDef *def=den_item_def(item.base_id);
    int i,at;
    if(!s||!def||!den_item_find_space(s,def,&at))return 0;
    for(i=0;i<DEN_ITEM_RECORDS;i++)if(!s->items[i].base_id)break;
    if(i==DEN_ITEM_RECORDS)return 0;
    s->items[i]=item;den_item_stamp(s,i,at);
    if(out_index)*out_index=i;
    return 1;
}

static inline int den_inventory_remove(DenItemState *s, int index)
{
    if(!s||index<0||index>=DEN_ITEM_RECORDS||!s->items[index].base_id||den_item_is_equipped(s,index))return 0;
    den_item_unstamp(s,index);s->items[index]=(DenItem){0};return 1;
}

static inline int den_item_requirements(const DenItem *item, int str, int dex, int level)
{
    const DenItemDef *def=item?den_item_def(item->base_id):0;
    return def&&str>=def->req_str&&dex>=def->req_dex&&level>=def->req_level;
}

/* Equip/unequip uses a copy so a displaced item is never lost on a full grid. */
static inline int den_item_equip(DenItemState *s, int index, int slot, int str, int dex, int level)
{
    DenItemState next;
    const DenItemDef *def;
    u8 old;
    int at;
    if(!s||index<0||index>=DEN_ITEM_RECORDS||slot<0||slot>=DEN_EQUIP_SLOTS)return 0;
    def=den_item_def(s->items[index].base_id);
    if(!def||def->equip_slot!=slot||!den_item_requirements(&s->items[index],str,dex,level)||den_item_is_equipped(s,index))return 0;
    next=*s;den_item_unstamp(&next,index);old=next.equipment[slot];next.equipment[slot]=(u8)(index+1);
    if(old){int oldidx=(int)old-1;const DenItemDef *olddef=den_item_def(next.items[oldidx].base_id);
        if(!olddef||!den_item_find_space(&next,olddef,&at))return 0;
        den_item_stamp(&next,oldidx,at);
    }
    *s=next;return 1;
}

static inline int den_item_unequip(DenItemState *s, int slot)
{
    DenItemState next;
    int index,at;
    const DenItemDef *def;
    if(!s||slot<0||slot>=DEN_EQUIP_SLOTS||!s->equipment[slot])return 0;
    next=*s;index=(int)next.equipment[slot]-1;def=den_item_def(next.items[index].base_id);
    if(!def||!den_item_find_space(&next,def,&at))return 0;
    next.equipment[slot]=0;den_item_stamp(&next,index,at);*s=next;return 1;
}

static inline int den_item_make(DenItem *out, u8 id, u16 seed)
{
    const DenItemDef *def=den_item_def(id);
    u32 x=seed?seed:0x9e3779b9u;
    if(!out||!def)return 0;
    x^=x<<13;x^=x>>17;x^=x<<5;
    *out=(DenItem){id,DEN_ITEM_QUALITY_NORMAL,def->max_durability,0,
        (u16)(def->min_defense+(def->max_defense>def->min_defense?x%(def->max_defense-def->min_defense+1):0)),seed};
    return 1;
}

static inline void den_item_stats(const DenItemState *s, int str, int dex, int level, DenItemStats *out)
{
    int i;const DenItemDef *weapon=0,*shield=0;
    if(!out)return;
    *out=(DenItemStats){0};
    if(!s)return;
    for(i=0;i<DEN_EQUIP_SLOTS;i++)if(s->equipment[i]) {
        const DenItem *item=&s->items[s->equipment[i]-1];const DenItemDef *def=den_item_def(item->base_id);
        if(!def||!den_item_requirements(item,str,dex,level)||(!item->durability&&def->max_durability))continue;
        if(def->category==DEN_ITEM_WEAPON)weapon=def;
        else out->defense=(s16)(out->defense+item->defense_roll);
        if(def->category==DEN_ITEM_SHIELD)shield=def;
    }
    if(weapon){out->min_damage=(s16)(weapon->min_damage*(100+str)/100);out->max_damage=(s16)(weapon->max_damage*(100+str)/100);out->weapon_id=weapon->id;}
    else {out->min_damage=1;out->max_damage=2;}
    out->attack_rating=(u16)den_item_clamp(DEN_BARB_TO_HIT_FACTOR+5*(dex-7),0,65535);
    if(shield){out->shield_id=shield->id;out->block_chance=(u16)den_item_clamp(DEN_BARB_BLOCK_FACTOR+shield->block,0,75);}
}

static inline int den_item_hit_chance(int attack_rating, int defense, int attacker_level, int defender_level)
{
    int sum=attack_rating+defense,to_hit_factor=100;
    if(sum)to_hit_factor=100*attack_rating/sum;
    if(attacker_level<1)attacker_level=1;
    if(defender_level<1)defender_level=1;
    return den_item_clamp(2*attacker_level*to_hit_factor/(attacker_level+defender_level),5,95);
}

#endif
