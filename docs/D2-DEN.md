# Diablo II: Den of Evil GBA test

This optional local build tests a single Barbarian in a compact Den of Evil
encounter, connected to a compact Rogue Encampment with Akara. It is an educational GBA adaptation with converted original game
art, not the PC executable running on the GBA. The default original-art Diablo
prototype remains independently buildable without the downloaded files.

## Private data

The freely distributed Blizzard Windows demo supplies the classic artwork.
The old [Battle.net file index](https://classic.battle.net/files.shtml) links to
Blizzard's retired demo page. A preserved demo is available at
[Internet Archive](https://archive.org/details/DiabloIiDemo), with matching
metadata at [ModDB](https://www.moddb.com/games/diablo-2/downloads/diablo-ii-demo).
The demo is `138309685` bytes, MD5 `9ae5033551a078937cd5d1f388cd8438`.
The local download manifest records its SHA-256 and retrieval URL.

All archives, extracted members, decoded images and generated C data belong in
`.local/d2/`. All ROMs, saves, screenshots and emulator reports belong in
`build/` or `.local/`. These locations are ignored and excluded from public
source/release packages. The authored importer contains no original artwork.

The conversion runs on the Mac. The GBA reads compact indexed images and
integer tables directly; it never decompresses MPQ/DCC/DT1 files at runtime.
A shared 224-colour game palette leaves the first 32 entries for readable UI.
Sprites and terrain are reduced offline to one quarter of their PC dimensions.
The gameplay camera displays them at 2× that imported size, so the Barbarian,
monsters and nearby drops are readable on the handheld screen. The same bounded
terrain cache supplies the magnified view. A display-only palette curve lifts
shadows and midtones for the original GBA/SP LCD; dim text and borders are also
brighter. A bright ground ring identifies the player against similar floor
colors. The private source palette and save format are unchanged.
Inventory DC6 icons are reduced separately to fit the 10×4 grid; ground icons
are pre-scaled offline and use the same fixed 2× expansion as world sprites.
The art bundle includes eight directions, neutral/walk/attack/hit/death modes,
nine plain cave floor variations and three cave rock/pillar decorations.
The town bundle adds original Town DT1 floor tiles, Akara's eight-direction
neutral animation, and original inventory icons. Missing dedicated talking
frames use her neutral animation. The camp is an authored compact layout, not
the complete original town preset.

The importer composes body and equipped starting axe/buckler layers using COF
priority rows; it does not generate every possible equipment appearance.

## Prepare, build, and test

```sh
./dev doctor
./dev den prepare
./dev den status
./dev den build
./dev den test
./dev den play
```

`./dev den prepare` verifies the pinned reader dependencies, builds StormLib locally,
fetches the hash-verified free demo and converts it without running the installer.
`./dev den status` shows the private source/build state. Preparation is optional;
the default game does not require these files. Preparation needs Python 3, Git
and network access, CMake 3.10 or later, and a native C/C++ compiler with C++20
support. System zlib and bzip2 development libraries are optional; StormLib
has bundled fallbacks. mGBA is needed only to play.

The Den build uses generated cave, town and item headers in
`.local/d2/generated/`. Missing original
data is an error, rather than a silent switch to stand-in artwork.

The controller test accepts Akara's quest, enters and clears the encounter,
picks up and equips dropped items, returns to town, and claims the reward
through Akara's dialogue. It checks repeat conversations and save/load. Its trace can be replayed on the actual ARM ROM in mGBA. At each
checkpoint, a hash of meaningful state fields checks host/ARM agreement.
Only controller events are injected; health, enemies and quest state are read.
Timing uses the emulated GBA hardware timer, independently of desktop speed.

## Controls and objective

- D-pad moves relative to the screen. In town, approach Akara and press A.
  A accepts her quest; B closes the conversation. Akara heals life and mana.
- Approach the marked Den gate and press A to enter. Hold A for ordinary melee;
  B uses Bash for 2 mana. R drinks a quick-use potion.
- A picks up the nearest highlighted drop. Gold and potions go into their
  counters; equipment goes into the inventory. Pickups retain their rolled defense.
- L opens/closes inventory. Select cycles Inventory, Equipment and Character.
  The inventory is 10×4 cells with the original base-item footprints. D-pad
  selects a cell, A equips its item, and B drops it into the current area.
- Equipment: D-pad selects one of seven slots; A unequips into available space.
  Swaps fail without losing either item when there is insufficient room.
- Character: Up/Down selects an attribute or Bash; A spends an available point.
  B closes the Equipment or Character view. These panels pause combat.
- Select outside panels toggles the map. Start pauses; A saves and resumes,
  B resumes. Select on the title loads the last save.
- Kill every required enemy, including Corpsefire and revived Fallen. Return to
  the entrance and press A to leave. Speak to Akara, then press A to claim the
  one-time +1 skill point. Leaving the cave alone grants no reward. Play continues
  in town after the quest is complete.

Saves preserve town/cave location, quest flags, equipment, inventory, ground
items and each monster's previous drop. Version 1 Den saves migrate to the new
format; an already-completed old quest stays rewarded. SRAM uses two journal
records, and save buffers live in EWRAM rather than the small CPU stack.

## Fidelity boundary

The encounter uses a fixed authored cave with connected chambers and a compact
camp. Layout, monster placement, combat pacing, level thresholds and drop
probabilities are reduced test content. It does not import the original
procedural level generator or reproduce a particular PC seed.

Normal Fallen/Zombie/Shaman life ranges, base experience, and nine normal base
item definitions come from the demo tables. Hand Axe, Buckler, Cap, Quilted
Armor, Boots, Gloves, Sash, Club and Scimitar have source-derived footprints,
requirements, damage, defense ranges and durability. Weapon damage and armor
change combat results; attack rating uses the class factor and Dexterity rule,
and to-hit includes attack rating, defense and level ratio.
Classic Barbarian shield blocking uses the class block factor plus shield block,
without the expansion Dexterity formula. Successful hits can wear weapons (4%)
or weighted armor locations (10%); broken equipment stops contributing stats.
There is no repair NPC in this slice.

The quest follows the reference pending/granted distinction and grants the skill
point only in Akara's turn-in. The test still starts with Bash available. It uses
an authored, bounded normal-item/gold/potion drop pool rather than the complete
TreasureClass/quality/affix engine. Monster revival cannot produce duplicate loot.
Potions use a quick-use counter rather than the original belt grid; their healing
is simplified. Equipment changes stats but the character artwork retains the
starting axe/buckler composite.

There is no full town NPC simulation, shop/trade/repair system, equipment weapon
switching, full skill tree, cinematics, original audio, multiplayer or later
campaign. The separate original-art Diablo prototype remains independently
buildable.

## Reference implementation provenance

- D2MOO `98a3cc281a6a96708e881718934b8e4c99bbe9e2`:
  `source/D2Game/src/QUESTS/ACT1/A1Q1.cpp` (Den kill-all and reward),
  `source/D2Game/src/AI/AiThink.cpp` (Fallen flee and shaman revival),
  `source/D2Game/src/SKILLS/SkillBar.cpp` (Bash calculation),
  `source/D2Common/src/D2Inventory.cpp` (grid occupancy),
  `source/D2Common/src/Items/Items.cpp` (base item stats),
  `source/D2Common/src/Units/Units.cpp:2434` (classic/expansion block),
  `source/D2Game/src/UNIT/SUnitDmg.cpp:2283,2540` (durability weights, to-hit),
  `source/D2Game/src/ITEMS/Items.cpp:1651` (durability loss probabilities).
- D2Decomped `f344332ce4b2e42e54ca3b5d7c8e480f49d823ee`:
  `components/dcc/dcc.hpp`, `components/dt1/dt1.hpp`,
  `components/cof/cof.hpp`, `components/dc6/dc6.hpp` (offline art readers),
  `components/game/gamedata_load.cpp:55-140` (town asset members).
- StormLib v9.30 `86f9b99ffe4d3417dad16d00541cf6f2e3d7bf79`:
  archive access through its public API, built locally under `.local/d2/`.
- Official Blizzard [Arreat Summit](https://classic.battle.net/diablo2exp/):
  class records, Den objective and Bash skill description.

## Verified result

The brighter, magnified 1 MiB build averages **29.26 fps** across the full quest
on the emulated GBA CPU, with measured intervals at 28.81–29.80 fps and a
remaining worst-frame area-load hitch of about 0.29 seconds. It is tested with
sanitizer-backed inventory/save fixtures,
a controller-only full quest replay, ARM/host state and pixel comparisons, and
a fresh-process SRAM reload. Only visible entities enter the depth sort; hot
drawing code runs in IWRAM, and the existing terrain cache supplies a bounded
2× window expansion. Cached HUD bands use direct DMA copies.
These figures are from the source snapshot before this public export; they are
emulator measurements, not a claim of physical GBA SP performance. Hardware
testing remains separate from emulator verification.
