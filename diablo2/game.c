#ifndef D2_DEN_OF_EVIL
#include "../common/gba.h"

/* Diablo II-inspired, original-art GBA action RPG. Positions are eighth-tile
   fixed point; all simulation and rendering are integer-only. */
#define MAP_W 40
#define MAP_H 40
#define MOB_MAX 12
#define SHOT_MAX 8
#define DROP_MAX 12
#define BAG_MAX 8
#define SAVE_TAG 0x44324949u
#define SAVE_MAGIC 0x44324932u

enum { TITLE, PLAY, DEAD, WIN };
enum { BARBARIAN, SORCERESS, AMAZON };
enum { GRASS, DIRT, STONE, WATER, ROCK, GATE };
enum { ITEM_WEAPON, ITEM_ARMOR, ITEM_POTION, ITEM_GOLD };
enum { NORMAL, MAGIC, RARE, UNIQUE };

typedef struct {
    s16 x, y, hp, max_hp, level, damage, defense, attack_rating;
    u8 kind, alive, elite, flash;
} Mob;

typedef struct {
    s16 x, y, vx, vy, damage;
    u8 life, kind;
} Shot;

typedef struct {
    s16 x, y;
    s16 value;
    u8 type, rarity, prefix, suffix, alive;
} Item;

typedef struct {
    u32 magic, seed;
    u16 version, zone, xp, gold, level;
    u16 str, dex, vit, energy, skill, skill_points, stat_points;
    u16 potions, kills, hp, mana, stamina, weapon_damage, armor;
    u16 armor_buff, buff_timer;
    Item equipped_weapon, equipped_armor;
    s16 px, py;
    u8 class_id, skill_mode, map[MAP_H][MAP_W];
    Mob mobs[MOB_MAX];
    Item drops[DROP_MAX], bag[BAG_MAX];
    u8 bag_count, selected, gate_x, gate_y, quest;
} Save;

typedef struct {
    u32 seed;
    int screen, class_id, zone, level, xp, gold;
    int str, dex, vit, energy, skill, skill_points, stat_points;
    int potions, kills, hp, mana, stamina, weapon_damage, armor;
    int armor_buff, buff_timer, skill_mode;
    Item equipped_weapon, equipped_armor;
    int px, py, aim_x, aim_y, inventory, menu, selected_stat;
    int tick, attack_cooldown, skill_cooldown, notice, notice_time;
    int gate_x, gate_y, quest;
    u8 map[MAP_H][MAP_W];
    Mob mobs[MOB_MAX];
    Shot shots[SHOT_MAX];
    Item drops[DROP_MAX], bag[BAG_MAX];
    int bag_count, selected_item;
} Game;

static Game g;
static const char *class_names[] = { "BARBARIAN", "SORCERESS", "AMAZON" };
static const char *stat_names[] = { "STRENGTH", "DEXTERITY", "VITALITY", "ENERGY" };
static const char *prefix_names[] = { "", "FINE", "STURDY", "BRIGHT" };
static const char *suffix_names[] = { "", "OF POWER", "OF GUARDING", "OF MANA" };

static int abs2(int n) { return n < 0 ? -n : n; }
static int min2(int a, int b) { return a < b ? a : b; }
static int max2(int a, int b) { return a > b ? a : b; }
static u32 random32(void) { return rng_next(&g.seed); }
static int distance(int x, int y, int a, int b) {
    return abs2(x - a) + abs2(y - b);
}
static int tile_x(int x) { return x / 8; }
static int tile_y(int y) { return y / 8; }
static int passable(int x, int y) {
    if (x < 1 || y < 1 || x >= MAP_W - 1 || y >= MAP_H - 1) return 0;
    return g.map[y][x] != ROCK && g.map[y][x] != WATER;
}

static int map_path_exists(int start_x, int start_y, int end_x, int end_y) {
    int queue_x[MAP_W * MAP_H], queue_y[MAP_W * MAP_H];
    u8 seen[MAP_H][MAP_W] = {{0}};
    int head = 0, tail = 0;
    queue_x[tail] = start_x;
    queue_y[tail++] = start_y;
    seen[start_y][start_x] = 1;
    while (head < tail) {
        int x = queue_x[head], y = queue_y[head++];
        static const s8 dx[4] = { 1, -1, 0, 0 };
        static const s8 dy[4] = { 0, 0, 1, -1 };
        int d;
        if (x == end_x && y == end_y) return 1;
        for (d = 0; d < 4; ++d) {
            int nx = x + dx[d], ny = y + dy[d];
            if (passable(nx, ny) && !seen[ny][nx]) {
                seen[ny][nx] = 1;
                queue_x[tail] = nx;
                queue_y[tail++] = ny;
            }
        }
    }
    return 0;
}
static void notice(int id, int frames) { g.notice = id; g.notice_time = frames; }

/* Arreat Summit class records give starting totals, so only allocated
   attributes above the class baseline contribute additional life/mana. */
static int max_hp(void) {
    int base_vit=g.class_id==BARBARIAN?25:g.class_id==AMAZON?20:10;
    int per_vit=g.class_id==BARBARIAN?4:g.class_id==AMAZON?3:2;
    int base=g.class_id==BARBARIAN?55:g.class_id==AMAZON?50:40;
    int per_level=g.class_id==SORCERESS?1:2;
    return base+max2(0,g.vit-base_vit)*per_vit+max2(0,g.level-1)*per_level;
}
static int max_mana(void) {
    int base_energy=g.class_id==SORCERESS?35:g.class_id==AMAZON?15:10;
    int base=g.class_id==SORCERESS?35:g.class_id==AMAZON?15:10;
    int points=max2(0,g.energy-base_energy)+max2(0,g.level-1);
    return base+(g.class_id==SORCERESS?2*points:g.class_id==AMAZON?3*points/2:points);
}

static void clear_entities(void) {
    int i;
    for (i = 0; i < MOB_MAX; ++i) g.mobs[i].alive = 0;
    for (i = 0; i < SHOT_MAX; ++i) g.shots[i].life = 0;
    for (i = 0; i < DROP_MAX; ++i) g.drops[i].alive = 0;
}

/* Carve a connected cross-shaped clearing and a corridor to the gate after
   decorating the rest of the map. Every required enemy stays on the clearing. */
static void generate_map(void) {
    int x, y;
    for (y = 0; y < MAP_H; ++y) {
        for (x = 0; x < MAP_W; ++x) {
            if (x == 0 || y == 0 || x == MAP_W - 1 || y == MAP_H - 1) {
                g.map[y][x] = ROCK;
            } else {
                u32 r = random32() % 100;
                if (g.zone == 0)
                    g.map[y][x] = r < 8 ? ROCK : r < 15 ? WATER : GRASS;
                else
                    g.map[y][x] = r < 17 ? ROCK : r < 25 ? DIRT : STONE;
            }
        }
    }
    /* A broad road follows screen-right through the isometric world (+X,-Y). */
    {
        int step;
        for (step = 0; step <= 14; ++step) {
            int offset_x, offset_y;
            for (offset_x = -3; offset_x <= 3; ++offset_x) {
                for (offset_y = -3; offset_y <= 3; ++offset_y) {
                    int road_x = 20 + step + offset_x;
                    int road_y = 20 - step + offset_y;
                    if (abs2(offset_x) + abs2(offset_y) <= 4 && road_x > 0 && road_y > 0 &&
                        road_x < MAP_W - 1 && road_y < MAP_H - 1)
                        g.map[road_y][road_x] = g.zone ? STONE : (offset_x == 0 && offset_y == 0 ? DIRT : GRASS);
                }
            }
        }
    }
    g.gate_x = 34;
    g.gate_y = 6;
    g.map[g.gate_y][g.gate_x] = GATE;
}

static void spawn_mob(int slot, int x, int y, int elite) {
    Mob *m = &g.mobs[slot];
    m->x = (s16)(x * 8 + 4);
    m->y = (s16)(y * 8 + 4);
    m->level = (s16)(1 + g.zone + (elite ? 2 : 0));
    m->elite = (u8)elite;
    m->kind = (u8)(g.zone ? 1 + slot % 2 : slot % 2);
    m->max_hp = (s16)((g.zone ? 24 : 16) + g.level * 3 + (elite ? 58 : 0));
    m->hp = m->max_hp;
    m->damage = (s16)((g.zone ? 4 : 2) + g.level + (elite ? 5 : 0));
    m->attack_rating = 45 + m->level * 15 + (elite ? 25 : 0);
    m->defense = (s16)(5 + m->level * 2);
    m->alive = 1;
    m->flash = 0;
}

static void spawn_pack(void) {
    int i;
    for (i = 0; i < MOB_MAX; ++i) g.mobs[i].alive = 0;
    /* Predictable spawn points avoid overlaps and unreachable stragglers. */
    spawn_mob(0, 24, 16, 0);
    spawn_mob(1, 28, 12, 0);
    spawn_mob(2, 31, 9, 0);
    spawn_mob(3, 26, 14, 0);
    spawn_mob(4, 23, 19, 0);
    spawn_mob(5, 33, 7, 0);
    spawn_mob(6, 21, 21, 0);
    if (g.zone) spawn_mob(7, 32, 8, 1);
    g.quest = g.zone;
}

/* D2MOO's integer chance formula, including its intermediate ratio floor. */
static int chance_to_hit(int ar, int defense, int attacker_level, int defender_level) {
    int den = ar + defense;
    int level_den = attacker_level + defender_level;
    if (den <= 0 || level_den <= 0 || ar <= 0 || attacker_level <= 0) return 5;
    return clampi((2 * attacker_level * (100 * ar / den)) / level_den, 5, 95);
}

static int player_defense(void) {
    return 5 + g.dex / 4 + g.armor + g.armor_buff;
}

static int enemy_hit_chance(const Mob *m) {
    return chance_to_hit(m->attack_rating, player_defense(), m->level, g.level);
}

static void save_game(void) {
    Save s = {0};
    s.magic = SAVE_MAGIC;
    s.version = 4;
    s.seed = g.seed;
    s.zone = (u16)g.zone;
    s.xp = (u16)g.xp;
    s.gold = (u16)g.gold;
    s.level = (u16)g.level;
    s.str = (u16)g.str;
    s.dex = (u16)g.dex;
    s.vit = (u16)g.vit;
    s.energy = (u16)g.energy;
    s.skill = (u16)g.skill;
    s.skill_points = (u16)g.skill_points;
    s.stat_points = (u16)g.stat_points;
    s.potions = (u16)g.potions;
    s.kills = (u16)g.kills;
    s.hp = (u16)g.hp;
    s.mana = (u16)g.mana;
    s.stamina = (u16)g.stamina;
    s.weapon_damage = (u16)g.weapon_damage;
    s.armor = (u16)g.armor;
    s.armor_buff = (u16)g.armor_buff;
    s.buff_timer = (u16)g.buff_timer;
    s.equipped_weapon = g.equipped_weapon;
    s.equipped_armor = g.equipped_armor;
    s.px = (s16)g.px;
    s.py = (s16)g.py;
    s.class_id = (u8)g.class_id;
    s.skill_mode = (u8)g.skill_mode;
    s.bag_count = (u8)g.bag_count;
    s.selected = (u8)g.selected_item;
    s.gate_x = (u8)g.gate_x;
    s.gate_y = (u8)g.gate_y;
    s.quest = (u8)g.quest;
    {
        int y;
        for (y = 0; y < MAP_H; ++y) {
            int x;
            for (x = 0; x < MAP_W; ++x) s.map[y][x] = g.map[y][x];
        }
    }
    {
        int i;
        for (i = 0; i < MOB_MAX; ++i) s.mobs[i] = g.mobs[i];
        for (i = 0; i < DROP_MAX; ++i) s.drops[i] = g.drops[i];
        for (i = 0; i < BAG_MAX; ++i) s.bag[i] = g.bag[i];
    }
    save_write(&s, (u32)sizeof(s), SAVE_TAG);
}

static int valid_save(const Save *s) {
    int px = tile_x(s->px), py = tile_y(s->py);
    return s->magic == SAVE_MAGIC && s->version == 4 && s->class_id < 3 && s->skill_mode < 3 &&
           s->zone < 2 && s->level > 0 && s->level <= 99 &&
           s->bag_count <= BAG_MAX && s->gate_x > 0 && s->gate_x < MAP_W - 1 &&
           s->gate_y > 0 && s->gate_y < MAP_H - 1 &&
           px > 0 && px < MAP_W - 1 && py > 0 && py < MAP_H - 1 &&
           s->map[py][px] != ROCK && s->map[py][px] != WATER;
}

static int load_game(void) {
    Save s;
    int y;
    if (!save_read(&s, (u32)sizeof(s), SAVE_TAG) || !valid_save(&s)) return 0;
    g.seed = s.seed ? s.seed : 1;
    g.zone = s.zone;
    g.class_id = s.class_id;
    g.xp = s.xp;
    g.gold = s.gold;
    g.level = s.level;
    g.str = s.str;
    g.dex = s.dex;
    g.vit = s.vit;
    g.energy = s.energy;
    g.skill = max2(1, s.skill);
    g.skill_points = s.skill_points;
    g.stat_points = s.stat_points;
    g.potions = min2(9, s.potions);
    g.kills = s.kills;
    g.hp = min2(max_hp(), s.hp);
    g.mana = min2(max_mana(), s.mana);
    g.stamina = min2(100, s.stamina);
    g.weapon_damage = max2(0, s.weapon_damage);
    g.armor = max2(0, s.armor);
    g.armor_buff = s.armor_buff;
    g.buff_timer = s.buff_timer;
    g.equipped_weapon = s.equipped_weapon;
    g.equipped_armor = s.equipped_armor;
    g.skill_mode = s.skill_mode;
    g.px = s.px;
    g.py = s.py;
    g.gate_x = s.gate_x;
    g.gate_y = s.gate_y;
    g.quest = s.quest;
    g.bag_count = s.bag_count;
    g.selected_item = s.selected < BAG_MAX ? s.selected : 0;
    for (y = 0; y < MAP_H; ++y) {
        int x;
        for (x = 0; x < MAP_W; ++x) g.map[y][x] = s.map[y][x] <= GATE ? s.map[y][x] : ROCK;
    }
    {
        int i;
        for (i = 0; i < MOB_MAX; ++i) g.mobs[i] = s.mobs[i];
        for (i = 0; i < DROP_MAX; ++i) g.drops[i] = s.drops[i];
        for (i = 0; i < BAG_MAX; ++i) g.bag[i] = s.bag[i];
    }
    for (y = 0; y < SHOT_MAX; ++y) g.shots[y].life = 0;
    g.screen = PLAY;
    g.inventory = 0;
    g.menu = 0;
    g.attack_cooldown = 0;
    g.skill_cooldown = 0;
    return 1;
}

static void start_game(int class_id) {
    int i;
    g.seed = 0x91a2b3cdu;
    g.screen = PLAY;
    g.class_id = class_id;
    g.zone = 0;
    g.level = 1;
    g.xp = 0;
    g.gold = 0;
    g.kills = 0;
    g.potions = 3;
    g.skill = 1;
    g.skill_mode = 0;
    g.skill_points = 0;
    g.stat_points = 0;
    g.weapon_damage = 0;
    g.armor = 0;
    g.equipped_weapon = (Item){0};
    g.equipped_weapon.type = ITEM_WEAPON;
    g.equipped_weapon.alive = 1;
    g.equipped_armor = (Item){0};
    g.equipped_armor.type = ITEM_ARMOR;
    g.equipped_armor.alive = 1;
    g.armor_buff = 0;
    g.buff_timer = 0;
    g.bag_count = 0;
    g.selected_item = 0;
    g.str = class_id == BARBARIAN ? 30 : class_id == AMAZON ? 20 : 10;
    g.dex = class_id == AMAZON ? 25 : class_id == SORCERESS ? 25 : 20;
    g.vit = class_id == BARBARIAN ? 25 : class_id == AMAZON ? 20 : 10;
    g.energy = class_id == SORCERESS ? 35 : class_id == AMAZON ? 15 : 10;
    g.hp = max_hp();
    g.mana = max_mana();
    g.stamina = 100;
    g.px = 20 * 8 + 4;
    g.py = 20 * 8 + 4;
    g.aim_x = 1;
    g.aim_y = 0;
    g.inventory = 0;
    g.menu = 0;
    g.tick = 0;
    g.attack_cooldown = 0;
    g.skill_cooldown = 0;
    g.notice = 0;
    g.notice_time = 0;
    g.selected_stat = 0;
    for (i = 0; i < BAG_MAX; ++i) g.bag[i].alive = 0;
    clear_entities();
    generate_map();
    spawn_pack();
    audio_tone(660, 5, 2);
}

static void gain_experience(int amount) {
    g.xp += amount;
    while (g.xp >= g.level * 100) {
        g.xp -= g.level * 100;
        ++g.level;
        ++g.skill_points;
        g.stat_points += 5;
        g.hp = max_hp();
        g.mana = max_mana();
        notice(10, 75);
    }
}

static int nearest_mob(void) {
    int i, found = -1, best = 10000;
    for (i = 0; i < MOB_MAX; ++i) {
        if (g.mobs[i].alive) {
            int d = distance(g.px, g.py, g.mobs[i].x, g.mobs[i].y);
            if (d < best) { best = d; found = i; }
        }
    }
    return found;
}

static void add_drop(Item item) {
    int i;
    for (i = 0; i < DROP_MAX; ++i) {
        if (!g.drops[i].alive) { g.drops[i] = item; g.drops[i].alive = 1; return; }
    }
}

static void make_loot(Mob *m) {
    Item item = {0};
    u32 r = random32() % 100;
    item.x = m->x;
    item.y = m->y;
    item.type = ITEM_GOLD;
    item.value = (s16)(3 + random32() % 10 + (m->elite ? 20 : 0));
    item.alive = 1;
    add_drop(item);
    if (r < 12) {
        item.type = ITEM_POTION;
        item.value = 1;
        add_drop(item);
    }
    if (m->elite || r < 30) {
        item.type = (u8)(random32() % 2);
        item.rarity = (u8)(m->elite ? RARE + (random32() % 2) : 1 + (random32() % 2));
        item.prefix = (u8)(random32() % 4);
        item.suffix = (u8)(random32() % 4);
        item.value = (s16)(2 + g.level + random32() % 5 + (item.rarity * 2));
        item.alive = 1;
        add_drop(item);
    }
}

static void kill_mob(Mob *m) {
    m->alive = 0;
    ++g.kills;
    gain_experience(m->elite ? 90 : 22);
    make_loot(m);
    notice(m->elite ? 2 : 1, 65);
    audio_tone(m->elite ? 220 : 430, 5, 2);
}

static void player_attack(void) {
    int i = nearest_mob();
    int ar, hit, damage;
    Mob *m;
    if (i < 0) return;
    m = &g.mobs[i];
    if (distance(g.px, g.py, m->x, m->y) > 20) return;
    ar = 50 + g.dex * 4;
    hit = chance_to_hit(ar, m->defense, g.level, m->level);
    if ((int)(random32() % 100) >= hit) { notice(3, 25); return; }
    damage = 3 + g.str / 5 + (int)(random32() % 4) + g.weapon_damage;
    if (g.class_id == BARBARIAN) damage += 2 + g.skill / 2;
    m->hp -= (s16)damage;
    m->flash = 8;
    if (m->hp <= 0) kill_mob(m);
    audio_tone(110, 3, 2);
}

static void fire_projectile(int kind, int spread) {
    int i, target = nearest_mob();
    int tx = g.px + g.aim_x * 40;
    int ty = g.py + g.aim_y * 40;
    /* Soft targeting toward a nearby monster; movement direction remains the
       fallback so a blocked or distant foe cannot hijack the spell's bearing. */
    if (target >= 0 && distance(g.px, g.py, g.mobs[target].x, g.mobs[target].y) <= 64) {
        tx = g.mobs[target].x;
        ty = g.mobs[target].y;
    }
    tx += spread * 6;
    ty -= spread * 6;
    for (i = 0; i < SHOT_MAX; ++i) {
        Shot *s = &g.shots[i];
        if (!s->life) {
            int dx = tx - g.px, dy = ty - g.py;
            int span = max2(abs2(dx), abs2(dy));
            if (!span) span = 1;
            s->x = (s16)g.px; s->y = (s16)g.py;
            s->vx = (s16)(dx * 3 / span); s->vy = (s16)(dy * 3 / span);
            s->damage = (s16)(kind == 1 ? 8 + g.skill * 2 : 5 + g.skill + g.weapon_damage);
            s->life = 16; s->kind = (u8)kind;
            audio_tone(kind == 1 ? 880 : 540, 7, 3);
            return;
        }
    }
}

static void use_skill(void) {
    int i;
    if (g.class_id == BARBARIAN) {
        if (g.skill_mode == 2) {
            if (g.stamina < 10) { notice(4, 30); return; }
            g.stamina -= 10;
            g.armor_buff = (u16)(8 + g.skill * 2);
            g.buff_timer = 120;
            notice(16, 45);
            audio_tone(290, 9, 3);
            return;
        }
        {
            int cost = g.skill_mode == 1 ? 25 : 15;
            int radius = g.skill_mode == 1 ? 48 : 30;
            if (g.stamina < cost) { notice(4, 30); return; }
            g.stamina -= cost;
            for (i = 0; i < MOB_MAX; ++i) {
                Mob *m = &g.mobs[i];
                if (m->alive && distance(g.px, g.py, m->x, m->y) < radius) {
                    m->hp -= (s16)((g.skill_mode == 1 ? 9 : 6) + g.skill * 3 + g.weapon_damage);
                    m->flash = 8;
                    if (m->hp <= 0) kill_mob(m);
                }
            }
        }
        audio_tone(g.skill_mode == 1 ? 120 : 150, 9, 3);
        return;
    }
    if (g.class_id == SORCERESS && g.skill_mode == 2) {
        int tx = tile_x(g.px + g.aim_x * 24), ty = tile_y(g.py + g.aim_y * 24);
        if (g.mana < 12) { notice(5, 30); return; }
        if (passable(tx, ty)) { g.px = tx * 8 + 4; g.py = ty * 8 + 4; g.mana -= 12; audio_tone(1020, 8, 3); }
        return;
    }
    if (g.class_id == SORCERESS && g.skill_mode == 1) {
        int target = nearest_mob();
        int cx = target >= 0 ? g.mobs[target].x : g.px + g.aim_x * 24;
        int cy = target >= 0 ? g.mobs[target].y : g.py + g.aim_y * 24;
        if (g.mana < 11) { notice(5, 30); return; }
        g.mana -= 11;
        for (i = 0; i < MOB_MAX; ++i) if (g.mobs[i].alive && distance(cx, cy, g.mobs[i].x, g.mobs[i].y) < 28) {
            g.mobs[i].hp -= (s16)(7 + g.skill * 2);
            g.mobs[i].flash = 8;
            if (g.mobs[i].hp <= 0) kill_mob(&g.mobs[i]);
        }
        notice(17, 30);
        audio_tone(740, 10, 3);
        return;
    }
    if (g.class_id == AMAZON && g.skill_mode == 2) {
        int target = nearest_mob();
        int cx = target >= 0 ? g.mobs[target].x : g.px + g.aim_x * 24;
        int cy = target >= 0 ? g.mobs[target].y : g.py + g.aim_y * 24;
        if (g.mana < 10) { notice(5, 30); return; }
        g.mana -= 10;
        for (i = 0; i < MOB_MAX; ++i) if (g.mobs[i].alive && distance(cx, cy, g.mobs[i].x, g.mobs[i].y) < 24) {
            g.mobs[i].hp -= (s16)(9 + g.skill * 2 + g.weapon_damage);
            g.mobs[i].flash = 8;
            if (g.mobs[i].hp <= 0) kill_mob(&g.mobs[i]);
        }
        notice(18, 30);
        audio_tone(600, 10, 3);
        return;
    }
    {
        int cost = g.class_id == SORCERESS ? 7 : g.skill_mode == 1 ? 8 : 4;
        if (g.mana < cost) { notice(5, 30); return; }
        if (g.class_id == AMAZON && g.skill_mode == 1) {
            g.mana -= cost;
            fire_projectile(2, -1);
            fire_projectile(2, 0);
            fire_projectile(2, 1);
        } else {
            g.mana -= cost;
            fire_projectile(g.class_id == SORCERESS ? 1 : 2, 0);
        }
    }
}

static void try_move(int dx, int dy, int amount) {
    int nx = g.px + dx * amount;
    int ny = g.py + dy * amount;
    /* Player coordinates are tile centers (tile*8+4), so divide directly. */
    if (passable(tile_x(nx), tile_y(ny))) { g.px = nx; g.py = ny; return; }
    if (dx && passable(tile_x(nx), tile_y(g.py))) g.px = nx;
    if (dy && passable(tile_x(g.px), tile_y(ny))) g.py = ny;
}

static void move_player(u16 held) {
    int dx = 0, dy = 0, amount;
    /* Map screen cardinal directions into isometric world axes. */
    if (held & KEY_LEFT) { --dx; ++dy; }
    if (held & KEY_RIGHT) { ++dx; --dy; }
    if (held & KEY_UP) { --dx; --dy; }
    if (held & KEY_DOWN) { ++dx; ++dy; }
    if (!dx && !dy) return;
    if (dx) g.aim_x = dx > 0 ? 1 : -1;
    if (dy) g.aim_y = dy > 0 ? 1 : -1;
    amount = (held & (KEY_LEFT | KEY_RIGHT)) ? 2 : 3;
    try_move(dx, dy, amount);
}

static void update_mobs(void) {
    int i;
    for (i = 0; i < MOB_MAX; ++i) {
        Mob *m = &g.mobs[i];
        if (!m->alive) continue;
        if (m->flash) --m->flash;
        if (distance(g.px, g.py, m->x, m->y) > 13) {
            if ((g.tick + i * 7) % 8 == 0) {
                int dx = g.px > m->x ? 1 : g.px < m->x ? -1 : 0;
                int dy = g.py > m->y ? 1 : g.py < m->y ? -1 : 0;
                int nx = m->x + dx * 2, ny = m->y + dy * 2;
                if (passable(tile_x(nx), tile_y(ny))) { m->x = (s16)nx; m->y = (s16)ny; }
                else if (passable(tile_x(nx), tile_y(m->y))) m->x = (s16)nx;
                else if (passable(tile_x(m->x), tile_y(ny))) m->y = (s16)ny;
            }
        } else if (g.tick % 24 == 0) {
            if ((int)(random32() % 100) < enemy_hit_chance(m)) {
                g.hp -= max2(1, m->damage);
                notice(6, 18);
                audio_tone(90, 2, 2);
                if (g.hp <= 0) { g.hp = 0; g.screen = DEAD; }
            } else {
                notice(3, 18);
            }
        }
    }
}

static void update_shots(void) {
    int i, j;
    for (i = 0; i < SHOT_MAX; ++i) {
        Shot *s = &g.shots[i];
        int tx, ty;
        if (!s->life) continue;
        s->x += s->vx; s->y += s->vy; --s->life;
        tx = tile_x(s->x); ty = tile_y(s->y);
        if (!passable(tx, ty)) { s->life = 0; continue; }
        for (j = 0; j < MOB_MAX; ++j) {
            Mob *m = &g.mobs[j];
            if (m->alive && distance(s->x, s->y, m->x, m->y) < 7) {
                m->hp -= s->damage; m->flash = 8;
                if (m->hp <= 0) kill_mob(m);
                s->life = 0;
                break;
            }
        }
    }
}

static void equip_selected(void) {
    Item *it;
    Item old;
    if (g.selected_item >= g.bag_count) return;
    it = &g.bag[g.selected_item];
    if (!it->alive) return;
    if (it->type == ITEM_WEAPON) {
        old = g.equipped_weapon;
        g.equipped_weapon = *it;
        g.equipped_weapon.alive = 1;
        *it = old;
        it->alive = 1;
        g.weapon_damage = g.equipped_weapon.value;
    } else if (it->type == ITEM_ARMOR) {
        old = g.equipped_armor;
        g.equipped_armor = *it;
        g.equipped_armor.alive = 1;
        *it = old;
        it->alive = 1;
        g.armor = g.equipped_armor.value;
    }
    notice(7, 40);
}

static void pickup_items(void) {
    int i;
    for (i = 0; i < DROP_MAX; ++i) {
        Item *it = &g.drops[i];
        if (!it->alive || distance(g.px, g.py, it->x, it->y) > 10) continue;
        if (it->type == ITEM_GOLD) {
            g.gold += it->value;
            it->alive = 0;
            notice(15, 35);
        } else if (it->type == ITEM_POTION) {
            if (g.potions < 9) { ++g.potions; it->alive = 0; notice(8, 40); }
        } else if (g.bag_count < BAG_MAX) {
            g.bag[g.bag_count++] = *it;
            it->alive = 0;
            notice(9, 50);
        }
    }
}

static void allocate_stat(void) {
    int *values[] = { &g.str, &g.dex, &g.vit, &g.energy };
    int old_max;
    if (!g.stat_points) { notice(11, 30); return; }
    old_max = g.selected_stat == 2 ? max_hp() : g.selected_stat == 3 ? max_mana() : 0;
    ++*values[g.selected_stat];
    --g.stat_points;
    if (g.selected_stat == 2) g.hp += max_hp() - old_max;
    if (g.selected_stat == 3) g.mana += max_mana() - old_max;
}

static void enter_gate(void) {
    int i, alive = 0;
    if (tile_x(g.px) != g.gate_x || tile_y(g.py) != g.gate_y) return;
    for (i = 0; i < MOB_MAX; ++i) if (g.mobs[i].alive) ++alive;
    if (alive) { notice(12, 45); return; }
    if (g.zone == 0) {
        g.zone = 1;
    g.px = 20 * 8 + 4;
    g.py = 20 * 8 + 4;
        generate_map();
        spawn_pack();
        notice(13, 90);
        save_game();
    } else {
        g.screen = WIN;
        save_game();
    }
}

void game_init(void) {
    g.seed = 0x91a2b3cdu;
    g.screen = TITLE;
    g.class_id = BARBARIAN;
    g.tick = 0;
    g.inventory = 0;
    g.menu = 0;
    g.notice = 0;
    g.notice_time = 0;
}

void game_update(u16 held, u16 pressed) {
    ++g.tick;
    if (g.notice_time) --g.notice_time;
    if (g.screen == TITLE) {
        if (pressed & KEY_LEFT) g.class_id = (g.class_id + 2) % 3;
        if (pressed & KEY_RIGHT) g.class_id = (g.class_id + 1) % 3;
        if (pressed & (KEY_A | KEY_START)) start_game(g.class_id);
        if (pressed & KEY_SELECT) load_game();
        return;
    }
    if (g.screen == DEAD || g.screen == WIN) {
        if (pressed & KEY_SELECT) g.screen = TITLE;
        else if (pressed & (KEY_A | KEY_START)) start_game(g.class_id);
        return;
    }
    if (pressed & KEY_SELECT) {
        g.skill_mode = (g.skill_mode + 1) % 3;
        notice(19, 45);
    }
    if (pressed & KEY_START) { g.menu = !g.menu; return; }
    if (g.menu) {
        if (pressed & KEY_A) { save_game(); g.menu = 0; notice(14, 45); }
        if (pressed & KEY_B) g.menu = 0;
        return;
    }
    if (pressed & KEY_L) g.inventory = !g.inventory;
    if ((pressed & KEY_R) && !g.inventory) {
        if (g.potions) {
            --g.potions;
            g.hp = min2(max_hp(), g.hp + 25);
            g.mana = min2(max_mana(), g.mana + 10);
            audio_tone(700, 8, 2);
        }
    }
    if (g.inventory) {
        if (pressed & KEY_UP) g.selected_stat = (g.selected_stat + 3) % 4;
        if (pressed & KEY_DOWN) g.selected_stat = (g.selected_stat + 1) % 4;
        if (pressed & KEY_LEFT && g.bag_count) g.selected_item = (g.selected_item + g.bag_count - 1) % g.bag_count;
        if (pressed & KEY_RIGHT && g.bag_count) g.selected_item = (g.selected_item + 1) % g.bag_count;
        if (pressed & KEY_A) {
            if (held & KEY_R) allocate_stat();
            else if (g.bag_count) equip_selected();
        }
        if (pressed & KEY_B) {
            if (g.skill_points) { ++g.skill; --g.skill_points; }
            else notice(11, 30);
        }
        return;
    }
    if (g.attack_cooldown) --g.attack_cooldown;
    if (g.skill_cooldown) --g.skill_cooldown;
    move_player(held);
    if ((held & KEY_A) && !g.attack_cooldown) { player_attack(); g.attack_cooldown = 12; }
    if ((pressed & KEY_B) && !g.skill_cooldown) { use_skill(); g.skill_cooldown = 18; }
    if (g.mana < max_mana() && g.tick % 8 == 0) ++g.mana;
    if (g.stamina < 100 && g.tick % 4 == 0) ++g.stamina;
    if (g.buff_timer && --g.buff_timer == 0 && g.armor_buff) {
        g.armor_buff = 0;
    }
    update_mobs();
    update_shots();
    pickup_items();
    if (g.screen == PLAY) enter_gate();
}

static int screen_x(int x, int y) { return 120 + (x - g.px - y + g.py); }
static int screen_y(int x, int y) { return 69 + ((x - g.px) + (y - g.py)) / 2; }

static void draw_tile(int sx, int sy, int type, int seed) {
    u8 color = type == GRASS ? C_GRASS : type == DIRT ? C_DIRT :
               type == STONE ? C_STONE : type == WATER ? C_BLUE_DARK :
               type == GATE ? C_GOLD_DARK : C_SHADOW;
    gfx_diamond(sx, sy, 8, 4, color);
    if (type == WATER) gfx_line(sx - 4, sy, sx + 3, sy, C_CYAN);
    else if (type == ROCK) gfx_diamond(sx, sy - 3, 3, 3, C_BROWN);
    else if (seed % 7 == 0) { gfx_pixel(sx - 3, sy - 1, C_GREEN); gfx_pixel(sx - 2, sy - 2, C_GREEN_DARK); }
    if (type == GATE) { gfx_rect(sx - 4, sy - 10, 2, 8, C_GOLD); gfx_rect(sx + 2, sy - 10, 2, 8, C_GOLD); gfx_line(sx - 4, sy - 10, sx + 4, sy - 10, C_GOLD); }
}

static void draw_hero(int sx, int sy) {
    u8 robe = g.class_id == BARBARIAN ? C_RED : g.class_id == SORCERESS ? C_BLUE : C_GREEN;
    int step = (g.tick / 4) & 1;
    gfx_diamond(sx, sy + 1, 5, 2, C_SHADOW);
    gfx_rect(sx - 3, sy - 10, 6, 7, C_SKIN);
    gfx_rect(sx - 4, sy - 5, 8, 7, robe);
    gfx_pixel(sx - 2, sy - 9, C_BLACK);
    gfx_pixel(sx + 2, sy - 9, C_BLACK);
    gfx_line(sx - 3, sy - 5, sx - 7, sy - 2 - step, C_SILVER);
    gfx_line(sx + 3, sy - 4, sx + 7, sy - 8 + step, g.class_id == SORCERESS ? C_CYAN : C_SILVER);
    gfx_pixel(sx + 7, sy - 8, g.class_id == SORCERESS ? C_CYAN : C_GOLD);
    gfx_line(sx - 2, sy + 2, sx - 3 + step * 2, sy + 4, C_BROWN);
    gfx_line(sx + 2, sy + 2, sx + 3 - step * 2, sy + 4, C_BROWN);
    gfx_pixel(sx, sy - 11, g.class_id == BARBARIAN ? C_GOLD : C_WHITE);
}

static void draw_portrait(int sx, int sy) {
    u8 robe = g.class_id == BARBARIAN ? C_RED : g.class_id == SORCERESS ? C_BLUE : C_GREEN;
    gfx_diamond(sx, sy + 13, 12, 4, C_SHADOW);
    gfx_rect(sx - 10, sy - 2, 20, 16, robe);
    gfx_rect(sx - 7, sy - 15, 14, 13, C_SKIN);
    gfx_rect(sx - 8, sy - 18, 16, 5, g.class_id == BARBARIAN ? C_GOLD_DARK : C_BROWN);
    gfx_pixel(sx - 3, sy - 10, C_BLACK);
    gfx_pixel(sx + 3, sy - 10, C_BLACK);
    gfx_line(sx - 8, sy + 1, sx - 15, sy + 6, C_SILVER);
    gfx_line(sx + 8, sy + 1, sx + 14, sy - 5, g.class_id == SORCERESS ? C_CYAN : C_SILVER);
    gfx_pixel(sx + 14, sy - 5, g.class_id == SORCERESS ? C_CYAN : C_GOLD);
    gfx_line(sx - 6, sy + 14, sx - 9, sy + 18, C_BROWN);
    gfx_line(sx + 6, sy + 14, sx + 9, sy + 18, C_BROWN);
}

static void draw_scenery(void) {
    static const u8 trees[][2] = { {6, 4}, {11, 5}, {3, 15}, {16, 14}, {8, 17} };
    int i;
    if (g.zone) {
        static const u8 pillars[][2] = { {6, 6}, {16, 6}, {6, 14}, {16, 14} };
        for (i = 0; i < 4; ++i) {
            int tx = pillars[i][0], ty = pillars[i][1];
            int sx = screen_x(tx * 8 + 4, ty * 8 + 4);
            int sy = screen_y(tx * 8 + 4, ty * 8 + 4);
            gfx_diamond(sx, sy + 2, 5, 2, C_SHADOW);
            gfx_rect(sx - 2, sy - 12, 4, 14, C_BROWN);
            gfx_line(sx - 2, sy - 10, sx + 2, sy - 10, C_SILVER);
        }
        return;
    }
    for (i = 0; i < 5; ++i) {
        int tx = trees[i][0], ty = trees[i][1];
        int sx, sy;
        if (!passable(tx, ty)) continue;
        sx = screen_x(tx * 8 + 4, ty * 8 + 4);
        sy = screen_y(tx * 8 + 4, ty * 8 + 4);
        gfx_diamond(sx, sy + 2, 5, 2, C_SHADOW);
        gfx_rect(sx - 1, sy - 8, 2, 9, C_BROWN);
        gfx_diamond(sx, sy - 10, 5, 7, C_GREEN_DARK);
        gfx_diamond(sx, sy - 13, 3, 5, C_GREEN);
    }
}

static void draw_mob(const Mob *m, int sx, int sy) {
    u8 color = m->flash ? C_WHITE : m->elite ? C_PURPLE : m->kind ? C_RED : C_ZERG;
    gfx_diamond(sx, sy + 1, 5, 2, C_SHADOW);
    if (m->kind == 0) {
        gfx_rect(sx - 4, sy - 8, 8, 7, color);
        gfx_rect(sx - 3, sy - 11, 6, 4, C_BROWN);
        gfx_pixel(sx - 1, sy - 9, C_GOLD); gfx_pixel(sx + 2, sy - 9, C_GOLD);
    } else {
        gfx_rect(sx - 3, sy - 9, 6, 8, color);
        gfx_rect(sx - 4, sy - 12, 8, 4, C_BROWN);
        gfx_pixel(sx - 2, sy - 11, C_RED); gfx_pixel(sx + 2, sy - 11, C_RED);
    }
    if (m->elite) {
        gfx_pixel(sx, sy - 14, C_GOLD);
        gfx_bar(sx - 7, sy - 17, 14, 2, m->hp, m->max_hp, C_RED_DARK, C_BLACK);
    }
}

static void draw_item(const Item *it, int sx, int sy) {
    u8 color = it->type == ITEM_GOLD ? C_GOLD : it->type == ITEM_POTION ? C_RED : it->rarity == UNIQUE ? C_ORANGE : it->rarity >= RARE ? C_GOLD : C_CYAN;
    gfx_diamond(sx, sy + 2, 3, 1, C_SHADOW);
    if (it->type == ITEM_GOLD) { gfx_circle(sx, sy - 2, 2, color); gfx_pixel(sx, sy - 2, C_GOLD_DARK); }
    else if (it->type == ITEM_POTION) { gfx_rect(sx - 2, sy - 5, 4, 6, color); gfx_rect(sx - 1, sy - 7, 2, 2, C_WHITE); }
    else { gfx_rect(sx - 3, sy - 4, 6, 4, color); gfx_pixel(sx, sy - 5, C_WHITE); }
}

static void draw_actor_sorted(void) {
    int order[MOB_MAX + DROP_MAX + 1], depth[MOB_MAX + DROP_MAX + 1];
    int count = 0, i, j;
    for (i = 0; i < MOB_MAX; ++i) if (g.mobs[i].alive) { order[count] = i; depth[count++] = g.mobs[i].x + g.mobs[i].y; }
    for (i = 0; i < DROP_MAX; ++i) if (g.drops[i].alive) { order[count] = MOB_MAX + i; depth[count++] = g.drops[i].x + g.drops[i].y; }
    order[count] = MOB_MAX + DROP_MAX; depth[count++] = g.px + g.py;
    for (i = 1; i < count; ++i) {
        int o = order[i], d = depth[i];
        for (j = i; j > 0 && depth[j - 1] > d; --j) { order[j] = order[j - 1]; depth[j] = depth[j - 1]; }
        order[j] = o; depth[j] = d;
    }
    for (i = 0; i < count; ++i) {
        int o = order[i];
        if (o < MOB_MAX) draw_mob(&g.mobs[o], screen_x(g.mobs[o].x, g.mobs[o].y), screen_y(g.mobs[o].x, g.mobs[o].y));
        else if (o < MOB_MAX + DROP_MAX) { Item *it = &g.drops[o - MOB_MAX]; draw_item(it, screen_x(it->x, it->y), screen_y(it->x, it->y)); }
        else draw_hero(120, 69);
    }
}

static void draw_orb(int x,int y,int radius,int value,int maximum,u8 color) {
    gfx_circle(x,y,radius,C_EDGE);
    int r=radius-1;
    int fill=maximum>0?(r*2+1)*clampi(value,0,maximum)/maximum:0;
    for(int yy=-r;yy<=r;yy++) {
        int span=r;
        while(span*span+yy*yy>r*r)span--;
        u8 shade=yy>r-fill?color:(color==C_BLUE?C_BLUE_DARK:C_RED_DARK);
        gfx_rect(x-span,y+yy,span*2+1,1,shade);
    }
}

static void draw_inventory(void) {
    Item *it;
    gfx_rect(33, 28, 174, 111, C_BLACK);
    gfx_frame(33, 28, 174, 111, C_GOLD);
    gfx_text(90, 34, "CHARACTER", C_GOLD);
    gfx_text(42, 48, stat_names[g.selected_stat], C_CYAN);
    gfx_text(150, 48, "UP/DN", C_DIM);
    gfx_text(42, 61, "STR", C_DIM); gfx_num(66, 61, g.str, C_WHITE);
    gfx_text(91, 61, "DEX", C_DIM); gfx_num(115, 61, g.dex, C_WHITE);
    gfx_text(142, 61, "VIT", C_DIM); gfx_num(166, 61, g.vit, C_WHITE);
    gfx_text(42, 72, "ENE", C_DIM); gfx_num(66, 72, g.energy, C_WHITE);
    gfx_text(91, 72, "SKILL", C_DIM); gfx_num(127, 72, g.skill, C_WHITE);
    gfx_text(151, 72, "PTS", C_GOLD); gfx_num(175, 72, g.skill_points, C_WHITE);
    gfx_text(42, 83, "STAT PTS", C_GOLD); gfx_num(96, 83, g.stat_points, C_WHITE);
    gfx_text(129, 83, "WPN", C_DIM); gfx_num(153, 83, 3 + g.str / 5 + g.weapon_damage, C_WHITE);
    gfx_text(165, 83, "ARM", C_DIM); gfx_num(189, 83, g.armor, C_WHITE);
    gfx_text(42, 94, "BAG", C_GOLD);
    if (!g.bag_count) gfx_text(72, 94, "EMPTY", C_DIM);
    else {
        it = &g.bag[g.selected_item];
        gfx_text(72, 94, it->type == ITEM_WEAPON ? "WEAPON" : "ARMOR", C_WHITE);
        gfx_text(126, 94, it->rarity == UNIQUE ? "UNIQUE" : it->rarity == RARE ? "RARE" : it->rarity == MAGIC ? "MAGIC" : "NORMAL", it->rarity >= RARE ? C_GOLD : C_CYAN);
        gfx_text(72, 106, prefix_names[it->prefix], C_CYAN);
        gfx_text(108, 106, suffix_names[it->suffix], C_DIM);
        gfx_text(173, 106, "V", C_DIM); gfx_num(184, 106, it->value, C_WHITE);
    }
    gfx_text(42, 123, "A EQUIP B SKILL R+A STAT", C_CYAN);
    gfx_text(42, 132, "LEFT/RIGHT: BAG ITEM", C_DIM);
}

static int remaining_mobs(void) {
    int i, count = 0;
    for (i = 0; i < MOB_MAX; ++i) if (g.mobs[i].alive) ++count;
    return count;
}

static void draw_minimap(void) {
    int mx, my;
    gfx_rect(216, 18, 20, 20, C_BLACK);
    gfx_frame(216, 18, 20, 20, C_GOLD_DARK);
    for (my = 0; my < 8; ++my) {
        for (mx = 0; mx < 8; ++mx) {
            int tx = min2(MAP_W - 1, mx * 5 + 2);
            int ty = min2(MAP_H - 1, my * 5 + 2);
            u8 color = g.map[ty][tx] == ROCK ? C_SHADOW :
                       g.map[ty][tx] == WATER ? C_BLUE :
                       g.map[ty][tx] == GATE ? C_GOLD :
                       g.zone ? C_STONE : C_GREEN_DARK;
            if (mx == g.gate_x / 5 && my == g.gate_y / 5) color = C_GOLD;
            if (mx == tile_x(g.px) / 5 && my == tile_y(g.py) / 5) color = C_WHITE;
            gfx_rect(218 + mx * 2, 20 + my * 2, 2, 2, color);
        }
    }
}

static const char *notice_text(int id) {
    switch (id) {
        case 1: return "MONSTER DOWN";
        case 2: return "ELITE DOWN";
        case 3: return "MISS";
        case 4: return "LOW STAMINA";
        case 5: return "LOW MANA";
        case 6: return "YOU ARE HIT";
        case 7: return "EQUIPPED";
        case 8: return "POTION FOUND";
        case 9: return "ITEM PICKED UP";
        case 10: return "LEVEL UP!";
        case 11: return "NO POINTS";
        case 12: return "CLEAR THE AREA";
        case 13: return "DESCENDING";
        case 14: return "SAVED";
        case 15: return "GOLD FOUND";
        case 16: return "BATTLE CRY";
        case 17: return "FROST NOVA";
        case 18: return "EXPLODING ARROW";
        case 19: return "SKILL CHANGED";
        default: return "";
    }
}

static const char *active_skill_name(void) {
    static const char *barbarian[] = { "SLAM", "WHIRL", "WARCRY" };
    static const char *sorceress[] = { "BOLT", "NOVA", "TELEPORT" };
    static const char *amazon[] = { "ARROW", "MULTI", "EXPLODE" };
    if (g.class_id == BARBARIAN) return barbarian[g.skill_mode];
    if (g.class_id == SORCERESS) return sorceress[g.skill_mode];
    return amazon[g.skill_mode];
}

void game_render(void) {
    int x, y;
    gfx_begin(g.screen == PLAY ? (g.zone ? C_STONE : C_GRASS) : C_BLACK);
    if (g.screen == TITLE) {
        gfx_rect(0,0,240,160,C_BLACK);
        gfx_text(93,22,"DIABLO II",C_GOLD);
        gfx_text(78,35,"GBA DEMAKE",C_RED);
        gfx_frame(26,49,188,66,C_GOLD_DARK);
        gfx_diamond(56,86,23,22,C_PANEL);
        draw_portrait(56,86);
        gfx_text(89,60,class_names[g.class_id],C_WHITE);
        gfx_text(89,75,g.class_id==BARBARIAN?"MELEE / WARCRIES":g.class_id==SORCERESS?"FIRE / ICE / WARP":"BOW / MULTISHOT",C_CYAN);
        gfx_text(89,95,"LEFT / RIGHT",C_DIM);
        gfx_text(48,124,"A: NEW ADVENTURE",C_WHITE);
        gfx_text(48,136,"SELECT: LOAD SAVE",C_CYAN);
        gfx_text(54,152,"GBA ARPG PROTOTYPE",C_DIM);
        gfx_end();
        return;
    }
    if (g.screen == DEAD || g.screen == WIN) {
        gfx_rect(0, 0, 240, 160, C_BLACK);
        gfx_text(82, 47, g.screen == DEAD ? "YOU HAVE DIED" : "VICTORY", g.screen == DEAD ? C_RED : C_GOLD);
        gfx_text(42, 72, g.screen == DEAD ? "YOUR JOURNEY ENDS" : "THE DEN IS CLEARED", C_WHITE);
        gfx_text(35, 100, "A: START AGAIN", C_CYAN);
        gfx_text(44, 112, "SELECT: TITLE", C_DIM);
        gfx_end();
        return;
    }
    /* Draw visible isometric cells from camera-relative sum/difference axes;
       cells beyond the generated border form a continuous terrain apron. */
    for (y = -12; y <= 16; ++y) {
        for (x = -14; x <= 14; ++x) {
            int dx, dy, tx, ty, sx, sy, type;
            if ((x + y) & 1) continue;
            dx = (x + y) / 2;
            dy = (y - x) / 2;
            tx = tile_x(g.px) + dx;
            ty = tile_y(g.py) + dy;
            sx = 120 + x * 8 + (g.py % 8) - (g.px % 8);
            sy = 69 + y * 4 + (8 - (g.px % 8) - (g.py % 8)) / 2;
            type = tx >= 0 && ty >= 0 && tx < MAP_W && ty < MAP_H ?
                   g.map[ty][tx] : g.zone ? STONE : GRASS;
            if (type == (g.zone ? STONE : GRASS)) {
                int detail = (tx * 13 + ty * 7) & 31;
                if (!detail) gfx_pixel(sx, sy, g.zone ? C_EDGE : C_GREEN_DARK);
                else if (detail == 1) gfx_pixel(sx - 1, sy + 1, g.zone ? C_DIM : C_GREEN);
            } else {
                draw_tile(sx, sy, type, tx * 3 + ty * 7);
            }
        }
    }
    draw_scenery();
    if (g.zone == 0) {
        gfx_diamond(screen_x(10 * 8, g.py) - 18, screen_y(10 * 8, g.py) - 10, 3, 10, C_GREEN_DARK);
        gfx_diamond(screen_x(13 * 8, g.py) + 20, screen_y(13 * 8, g.py) - 6, 4, 13, C_GREEN_DARK);
    }
    draw_actor_sorted();
    for (x = 0; x < SHOT_MAX; ++x) if (g.shots[x].life) {
        int sx = screen_x(g.shots[x].x, g.shots[x].y), sy = screen_y(g.shots[x].x, g.shots[x].y);
        gfx_circle(sx, sy, g.shots[x].kind == 1 ? 3 : 2, g.shots[x].kind == 1 ? C_CYAN : C_GOLD);
    }
    gfx_rect(0, 0, 240, 15, C_BLACK);
    gfx_text(3, 3, g.zone ? "CATACOMBS" : "BLOOD MOOR", C_GOLD);
    gfx_text(75, 3, "FOE", C_DIM); gfx_num(99, 3, remaining_mobs(), C_WHITE);
    gfx_text(126, 3, "LV", C_DIM); gfx_num(140, 3, g.level, C_WHITE);
    gfx_text(165, 3, "G", C_GOLD); gfx_num(177, 3, g.gold, C_WHITE);
    draw_minimap();
    gfx_rect(0, 138, 240, 22, C_BLACK); gfx_frame(0, 138, 240, 22, C_EDGE);
    draw_orb(12, 150, 8, g.hp, max_hp(), C_RED);
    draw_orb(228, 150, 8, g.mana, max_mana(), C_BLUE);
    gfx_num(22, 143, g.hp, C_WHITE);
    gfx_text(42, 143, class_names[g.class_id], C_WHITE);
    gfx_text(102, 143, "B:", C_DIM);
    gfx_text(114, 143, active_skill_name(), C_CYAN);
    gfx_text(180, 143, "MP", C_DIM); gfx_num(196, 143, g.mana, C_WHITE);
    gfx_bar(42, 153, 48, 3, g.xp, g.level * 100, C_GOLD, C_DIM);
    gfx_text(99, 152, "R POT", C_DIM); gfx_num(130, 152, g.potions, C_WHITE);
    gfx_text(151, 152, "SEL SKILL", C_DIM);
    if (g.notice_time) {
        gfx_rect(72, 21, 96, 10, C_BLACK);
        gfx_text(78, 23, notice_text(g.notice), C_GOLD);
    }
    if (g.inventory) draw_inventory();
    if (g.menu) {
        gfx_rect(67, 45, 106, 57, C_BLACK); gfx_frame(67, 45, 106, 57, C_GOLD);
        gfx_text(98, 54, "PAUSED", C_GOLD);
        gfx_text(81, 70, "A: SAVE GAME", C_WHITE);
        gfx_text(81, 83, "B: RESUME", C_DIM);
    }
    gfx_end();
}

/* Regression suite runs on the target or in the host harness. */
int game_self_test(void) {
    int failures = 0, i;
    if (chance_to_hit(100, 100, 1, 1) != 50) ++failures;
    if (chance_to_hit(1, 900, 1, 20) != 5) ++failures;
    if (chance_to_hit(900, 1, 20, 1) != 95) ++failures;
    if (chance_to_hit(100, 100, 1, 3) != 25) ++failures;
    g.seed = 0x12345678u;
    start_game(BARBARIAN);
    {
        Mob attacker = {0};
        attacker.attack_rating = 100;
        attacker.level = 1;
        g.level = 1;
        g.dex = 20;
        g.armor = 0;
        {
            int open_chance = enemy_hit_chance(&attacker);
            g.armor = 40;
            if (enemy_hit_chance(&attacker) >= open_chance) ++failures;
            if (enemy_hit_chance(&attacker) < 5 || enemy_hit_chance(&attacker) > 95) ++failures;
        }
    }
    if (g.screen != PLAY || g.hp != max_hp() || g.mobs[0].alive == 0) ++failures;
    if(g.hp!=55||g.mana!=10)++failures;
    start_game(SORCERESS);
    if(g.hp!=40||g.mana!=35)++failures;
    g.vit++;g.energy++;
    if(max_hp()!=42||max_mana()!=37)++failures;
    start_game(AMAZON);
    if(g.hp!=50||g.mana!=15)++failures;
    g.energy++;g.level++;
    if(max_hp()!=52||max_mana()!=18)++failures;
    start_game(BARBARIAN);
    /* Maps, enemy spawns, and objective remain connected across seeds/zones. */
    for (i = 0; i < 12; ++i) {
        int j;
        g.zone = i & 1;
        g.seed = (u32)(0x10203040u + i * 7919u);
        generate_map();
        spawn_pack();
        if (!map_path_exists(20, 20, g.gate_x, g.gate_y)) ++failures;
        for (j = 0; j < MOB_MAX; ++j) {
            if (g.mobs[j].alive && !map_path_exists(20, 20, tile_x(g.mobs[j].x), tile_y(g.mobs[j].y))) ++failures;
        }
    }
    start_game(BARBARIAN);
    /* Collision starts at tile center and cannot step through the next tile. */
    g.map[19][20] = ROCK;
    g.px = 19 * 8 + 4; g.py = 20 * 8 + 4;
    for (i = 0; i < 5; ++i) move_player(KEY_RIGHT);
    if (tile_x(g.px) == 20 && tile_y(g.py) == 19) ++failures;
    /* Gear exchange preserves the old item as the bag item. */
    g.bag_count = 1; g.bag[0] = (Item){0};
    g.bag[0].alive = 1; g.bag[0].type = ITEM_WEAPON; g.bag[0].value = 7;
    g.bag[0].rarity = RARE; g.bag[0].prefix = 2; g.bag[0].suffix = 3;
    g.weapon_damage = 0; g.selected_item = 0; equip_selected();
    if (g.weapon_damage != 7 || g.equipped_weapon.rarity != RARE ||
        g.bag[0].value != 0 || g.bag[0].type != ITEM_WEAPON ||
        g.bag[0].rarity != NORMAL) ++failures;
    /* Successful combat grants XP and places recoverable ground gold. */
    g.mobs[0].alive = 1; g.mobs[0].x = (s16)(g.px + 4); g.mobs[0].y = (s16)g.py;
    g.mobs[0].hp = 1; g.mobs[0].max_hp = 1; g.mobs[0].defense = 0; g.mobs[0].level = 1;
    g.dex = 1000;
    {
        int old_kills = g.kills;
        for (i = 0; i < 10 && g.mobs[0].alive; ++i) player_attack();
        if (g.mobs[0].alive || g.kills != old_kills + 1) ++failures;
    }
    {
        int found_gold = 0;
        for (i = 0; i < DROP_MAX; ++i) if (g.drops[i].alive && g.drops[i].type == ITEM_GOLD) found_gold = 1;
        if (!found_gold) ++failures;
    }
    /* The inventory spends the two independent level-up point currencies. */
    g.inventory = 1; g.stat_points = 1; g.selected_stat = 2; g.hp = max_hp();
    {
        int old_vit = g.vit, old_max = max_hp();
        game_update(KEY_R, KEY_A);
        if (g.vit != old_vit + 1 || g.stat_points || max_hp() <= old_max) ++failures;
    }
    g.skill_points = 1;
    {
        int old_skill = g.skill;
        game_update(0, KEY_B);
        if (g.skill != old_skill + 1 || g.skill_points) ++failures;
    }
    g.inventory = 0;
    /* Class-specific controls enforce their mana or stamina costs. */
    start_game(SORCERESS); g.skill_mode = 0; g.mana = 6;
    use_skill();
    if (g.mana != 6 || g.notice != 5) ++failures;
    start_game(AMAZON); g.skill_mode = 1; g.mana = max_mana();
    use_skill();
    {
        int shots = 0;
        for (i = 0; i < SHOT_MAX; ++i) if (g.shots[i].life) ++shots;
        if (shots != 3 || g.mana != max_mana() - 8) ++failures;
    }
    start_game(BARBARIAN); g.skill_mode = 2; g.stamina = 100;
    {
        int old_armor = g.armor;
        use_skill();
        if (g.armor != old_armor || !g.armor_buff || g.buff_timer != 120) ++failures;
        for (i = 0; i < MOB_MAX; ++i) g.mobs[i].alive = 0;
        for (i = 0; i < 120; ++i) game_update(0, 0);
        if (g.armor != old_armor || g.armor_buff) ++failures;
    }
    /* A potion is charged once and clamps to the derived maximum. */
    g.skill_mode = 0; g.potions = 1; g.hp = 1; g.mana = 1;
    game_update(0, KEY_R);
    if (g.hp != 26 || g.potions != 0) ++failures;
    /* Full character, map, live-monster, and item state survives a save cycle. */
    g.gold = 321; g.mobs[1].alive = 0; g.drops[0].alive = 1;
    g.class_id = SORCERESS;
    g.str = 10; g.dex = 25; g.vit = 10; g.energy = 35; g.level = 1;
    g.hp = max_hp(); g.mana = max_mana();
    g.equipped_weapon.rarity = RARE; g.equipped_weapon.prefix = 2;
    g.equipped_weapon.suffix = 3; g.equipped_weapon.value = 7;
    save_game();
    g.gold = 0; g.mobs[1].alive = 1; g.drops[0].alive = 0;
    g.class_id = BARBARIAN;
    if (!load_game() || g.gold != 321 || g.mobs[1].alive || !g.drops[0].alive ||
        g.class_id != SORCERESS || g.hp != 40 || g.mana != 35 ||
        g.equipped_weapon.rarity != RARE || g.equipped_weapon.prefix != 2 ||
        g.equipped_weapon.suffix != 3 || g.equipped_weapon.value != 7) ++failures;
    /* Death does not replace the saved game; a new run clears stale UI/entities. */
    g.screen = DEAD; g.gold = 777;
    game_update(0, 0);
    g.gold = 0;
    if (!load_game() || g.gold != 321) ++failures;
    g.screen = DEAD; g.bag_count = 1; g.menu = 1; g.shots[0].life = 4;
    game_update(KEY_A, KEY_A);
    if (g.bag_count != 0 || g.menu != 0 || g.shots[0].life != 0) ++failures;
    /* Long idle simulation must keep health and fixed entities bounded. */
    g.hp = max_hp(); g.mobs[0].alive = 0;
    for (i = 0; i < 240; ++i) game_update(0, 0);
    if (g.hp < 0 || g.hp > max_hp() || g.bag_count > BAG_MAX) ++failures;
    return failures;
}

#endif /* original-art build */
