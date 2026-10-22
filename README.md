# Diablo II: GBA demake

Diablo II: GBA demake is an original-art action RPG prototype for the Game Boy Advance. Choose a Barbarian, Sorceress, or Amazon, fight through a short two-zone run, collect and equip loot, level up, and defeat the final elite. It is a small demake with authored rules and generated maps, not a port of Diablo II or a recreation of its campaign.

## Get started

The default game builds and tests without a Diablo II installation, ROM, extracted game files, emulator, or network access. Install the [required tools](docs/DEVELOPMENT.md#install-the-tools), then clone the public source repository; no GitHub account is needed to download it:

```sh
git clone https://github.com/ggu/diablo2-gba.git
cd diablo2-gba
./dev doctor
./dev build
./dev test
```

To play, install mGBA and run `./dev play diablo2`. The controls and prototype limits are in [the game guide](diablo2/README.md). See [Contributing](CONTRIBUTING.md) for the asset-free workflow.

## Optional imported-art Den slice

The separate Den profile uses a free Blizzard Diablo II demo as a local source for original graphics and data. `./dev den prepare` downloads a pinned, hash-verified demo and prepares local-only converted files; it does not run the installer. Preparation needs Python 3, Git and network access, CMake 3.10 or later, and a native C/C++ compiler with C++20 support. System zlib and bzip2 development libraries are optional; StormLib has bundled fallbacks. It is not needed for default development.

```sh
./dev den prepare
./dev den status
./dev den build
./dev den test
./dev den play
```

The Den slice is a separate educational adaptation: one Barbarian can accept Akara's quest in a compact authored Rogue Encampment, enter a fixed cave, clear the quest, and return for the reward. It has imported original sprites and art, loot and inventory. The scene is magnified to 2× for handheld readability with brighter display colors. Its map is authored content, not the original campaign map or procedural generator. The demo, converted content, generated headers, ROMs, saves, and captures belong in ignored local directories and are not part of this source repository. See [the Den guide](docs/D2-DEN.md) and [provenance](SOURCE_PROVENANCE.md).

## Project guidance

AI agents should start with [AGENTS.md](AGENTS.md), which maps the two profiles, verification commands, and asset boundaries. Reference revisions and source locations are in the Den guide and [third-party notices](THIRD_PARTY_NOTICES.md).

## License

Copyright (C) 2026 Diablo II GBA contributors.

Project-authored files are free software under the GNU General Public License, version 3 or, at your option, any later version (`GPL-3.0-or-later`). See [LICENSE](LICENSE) for the grant and [LICENSES/GPL-3.0-or-later.txt](LICENSES/GPL-3.0-or-later.txt) for the full text. This program is distributed in the hope that it will be useful, but without any warranty; see the license for details. Preserve existing file-level copyright and license notices. Third-party material retains its own terms, described in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). This project license grants no rights to Diablo II or other Blizzard ROMs, artwork, maps, sounds, or game data.
