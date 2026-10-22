# Third-party notices and provenance

The default original-art game is authored for this repository and does not bundle Diablo II code or game data. The optional Den profile uses external projects as source references and local preparation dependencies. Their checkouts are fetched only into ignored local directories and are not vendored in this repository.

| Project | Pinned revision | License and use |
| --- | --- | --- |
| [D2MOO](https://github.com/ThePhrozenKeep/D2MOO) | `98a3cc281a6a96708e881718934b8e4c99bbe9e2` | MIT; gameplay behavior and values reference. See [LICENSES/D2MOO-MIT.txt](LICENSES/D2MOO-MIT.txt). |
| [D2Decomped](https://github.com/psi29a/d2decomped) | `f344332ce4b2e42e54ca3b5d7c8e480f49d823ee` | GPL-3.0-or-later; offline format-reader reference/dependency. See its upstream license and [GPL text](LICENSES/GPL-3.0-or-later.txt). |
| [StormLib](https://github.com/ladislav-zezula/StormLib) | `86f9b99ffe4d3417dad16d00541cf6f2e3d7bf79` | MIT; optional offline archive-reading dependency. See [LICENSES/StormLib-MIT.txt](LICENSES/StormLib-MIT.txt). |

The D2MOO and D2Decomped projects inform selected rules and offline asset formats. StormLib and the readers operate during optional preparation on the host; the GBA does not read MPQ, DCC, DT1, COF, or DC6 files at runtime. The reference implementation sources and the limited adaptation are documented in [docs/D2-DEN.md](docs/D2-DEN.md). Reference use does not mean the upstream projects' code or game data is part of the default game.

`tools/fix_rom.py` writes the standard 156-byte Nintendo GBA boot-logo sequence and cartridge header into generated ROMs. This hardware boot metadata is distinct from project-authored game art and is not covered by the project's GPL grant. The project does not claim ownership of or grant rights to Nintendo's material. No license described here grants rights to Blizzard's games or demo, their art, maps, sounds, or other content. Users are responsible for ensuring their use of game data is lawful.
