# Diablo II: GBA demake

This is a playable, original-art action RPG that condenses the Diablo II loop into a small GBA game. It has three selectable classes, an outdoor zone and a catacomb, class skills, monsters with simple pursuit and melee AI, drops, a small bag and equipment, leveling, potions, save/load, and a boss-gated ending. Both zones use generated 40×40 tile maps with a connected isometric road, enemy clearings, and a transition gate. It is a demake prototype, not a port: the campaign is short, there are two monster archetypes, and the art, sound, combat tuning, loot tables, and quest content are simplified.

## Controls

- D-pad: move in screen directions across the isometric world; the last direction points ranged skills
- A: hold to attack at a bounded rate
- B: use the selected class skill
- R: drink a healing and mana potion
- L: open or close the character and bag screen
- In the bag: Left/Right selects an item, A equips or swaps it, B spends a skill point, hold R and press A to spend a stat point, Up/Down selects the stat
- In the field: Select cycles among three class skills
- Start: pause; A saves and B resumes
- Title: Left/Right chooses a class, A starts, Select loads the last save
- Death or victory: A starts a new run, Select returns to the title

Kill every monster in the Blood Moor to open the Catacombs. Clear the monsters and elite in the second area to finish the run. Enemies leave gold and can drop potions and equipable weapons or armor. Gear has Normal, Magic, Rare, or Unique rarity labels with short affix names and a stat bonus. Gold and potions are collected by walking close to them. Each level grants one skill point and five stat points; spending stat points increases Strength, Dexterity, Vitality, or Energy. Equipping swaps the complete item record with the selected bag slot, including rarity and affixes. Select cycles three simplified skills per class: Barbarian Slam, Whirl, and Warcry; Sorceress Bolt, Nova, and Teleport; Amazon Arrow, Multishot, and Exploding Arrow.

## Fidelity notes

Melee hit chance follows D2MOO's level-weighted attack-rating rule and 5%–95% clamp, including integer truncation of `100 * toHit / (toHit + defense)` before applying the level ratio. See The Phrozen Keep's D2MOO commit [`98a3cc281a6a96708e881718934b8e4c99bbe9e2`](https://github.com/ThePhrozenKeep/D2MOO/tree/98a3cc281a6a96708e881718934b8e4c99bbe9e2), `source/D2Game/src/UNIT/SUnitDmg.cpp` near line 2540. Class starting attributes and base life/mana follow the classic class records summarized by the [Arreat Summit Barbarian](https://classic.battle.net/diablo2exp/classes/barbarian.shtml), [Amazon](https://classic.battle.net/diablo2exp/classes/amazon.shtml), and [Sorceress](https://classic.battle.net/diablo2exp/classes/sorceress.shtml) pages; the game uses fixed integer approximations for fractional Amazon mana growth. D2MOO's per-level life/mana growth and refill behavior is referenced in `source/D2Game/src/PLAYER/PlayerStats.cpp` near lines 96–103. The prototype uses hand-tuned combat stats and an invented XP curve; D2MOO loads class-specific experience thresholds and combat data from game tables, which are not reproduced here. No proprietary game assets or data are included.

Character base attributes and core life/mana growth mirror the three class starting records; there are three simplified rankable skills per class, shared skill ranks, and no full skill tree or resistance system. There is no town, NPC, mercenary, multiplayer, original monster catalog, or original item-generation engine. Sprites, tiles, UI, and audio cues are generated from code primitives.

Monster attack rating and raw damage are authored prototype values. Enemy attacks use the same level-weighted 5%–95% hit chance calculation against player defense (dexterity, equipped armor, and temporary armor buffs). Vitality and armor do not subtract a flat amount from successful physical hits; a full physical resistance and damage-type system is not implemented.

`game_self_test()` exercises the hit formula, multiple generated-map routes, collision at tile boundaries, kills and loot, equipment swaps, potion bounds, save/load of the current map and actors, death/restart behavior, and a bounded idle simulation. The shared Makefile supplies the build and sanitizer host harness.

## Optional original-artwork Den test

The separate [Den of Evil slice](../docs/D2-DEN.md) uses privately converted
Blizzard demo sprites and cave art, one Barbarian, and a completable kill-all
quest in a fixed authored cave. It has its own ROM/save tag and controls.
Prepare it with `./dev den prepare` and play it with `./dev den play`; see the
guide for setup, tests, and asset boundaries.
