#include "../diablo2/den_items.h"
#include <assert.h>
#include <string.h>

static DenItem make(u8 id,u16 seed)
{ DenItem item;assert(den_item_make(&item,id,seed));return item; }

int main(void)
{
    DenItemState s={0},before;
    DenItemStats stats;
    int axe,shield,cap,i;
    DenItem item;

    den_items_reset(&s);
    item=make(DEN_ITEM_HAND_AXE,7);assert(item.quality==DEN_ITEM_QUALITY_NORMAL&&item.durability==28);
    assert(den_inventory_add(&s,item,&axe)&&axe==0);
    item=make(DEN_ITEM_BUCKLER,8);assert(den_inventory_add(&s,item,&shield)&&shield==1);
    assert(s.grid[0]==1&&s.grid[1]==2&&s.grid[2]==2&&s.grid[11]==2&&s.grid[12]==2);
    assert(den_item_equip(&s,axe,DEN_SLOT_WEAPON,30,20,1));
    assert(den_item_equip(&s,shield,DEN_SLOT_SHIELD,30,20,1));
    den_item_stats(&s,30,20,1,&stats);
    assert(stats.weapon_id==DEN_ITEM_HAND_AXE&&stats.min_damage==3&&stats.max_damage==7);
    assert(stats.defense>=4&&stats.defense<=6&&stats.attack_rating==185);
    assert(stats.shield_id==DEN_ITEM_BUCKLER&&stats.block_chance==25);
    assert(den_item_unequip(&s,DEN_SLOT_WEAPON));
    assert(s.equipment[DEN_SLOT_WEAPON]==0&&s.grid[0]==1);
    assert(den_item_equip(&s,axe,DEN_SLOT_WEAPON,30,20,1));
    s.items[axe].durability=0;
    den_item_stats(&s,30,20,1,&stats);
    assert(stats.weapon_id==0&&stats.min_damage==1&&stats.max_damage==2);

    item=make(DEN_ITEM_SCIMITAR,2);assert(den_inventory_add(&s,item,&cap));
    before=s;
    assert(!den_item_equip(&s,cap,DEN_SLOT_WEAPON,30,20,1)); /* 21 DEX requirement */
    assert(memcmp(&s,&before,sizeof s)==0);
    assert(den_item_equip(&s,cap,DEN_SLOT_WEAPON,30,21,5));
    den_item_stats(&s,30,21,5,&stats);
    assert(stats.weapon_id==DEN_ITEM_SCIMITAR&&stats.min_damage==2&&stats.max_damage==7&&stats.attack_rating==190);

    /* Roll and durability are deterministic; normal items have no affix state. */
    assert(den_item_make(&item,DEN_ITEM_CAP,1234));
    DenItem again;assert(den_item_make(&again,DEN_ITEM_CAP,1234));
    assert(item.defense_roll==again.defense_roll&&item.flags==0&&item.quality==2);
    assert(item.defense_roll>=3&&item.defense_roll<=5);

    /* Fill the inventory with 2x2 helmets and prove failed add is atomic. */
    den_items_reset(&s);
    for(i=0;i<10;i++){assert(den_item_make(&item,DEN_ITEM_CAP,(u16)i));assert(den_inventory_add(&s,item,0));}
    before=s;assert(!den_inventory_add(&s,item,0));assert(memcmp(&s,&before,sizeof s)==0);
    assert(den_item_hit_chance(100,100,10,10)==50);
    assert(den_item_hit_chance(100,0,10,10)==95);
    assert(den_item_hit_chance(0,100,10,10)==5);
    assert(den_item_hit_chance(100,100,1,10)==9);
    assert(!den_inventory_remove(&s,-1));
    return 0;
}
