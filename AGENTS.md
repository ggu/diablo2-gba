# Agent guidance

## Project map

- The default original-art game is `diablo2/game.c`; it shares GBA startup, memory, display, and input support in `common/`.
- The optional original-artwork Den slice is selected with `D2_DEN_OF_EVIL` and lives in `diablo2/den_game.c` with its support headers. Its generated local headers are ignored.
- Host-side checks are in `tests/`. Build recipes are in `Makefile`; use the `dev` wrapper for documented workflows. Import and analysis tools are in `tools/`.
- Read `CONTRIBUTING.md` before changing asset handling or licensing, and `docs/D2-DEN.md` for the optional profile and its limits.

## Make changes safely

- Keep GBA runtime storage bounded. Use fixed-size data and integer arithmetic.
- Keep the default game independent of original Diablo II data. Never add ROMs, demo archives, extracted game data, converted assets, generated asset headers, or proprietary screenshots to source control.
- Keep the two profiles independently buildable. Do not make the default profile depend on optional Den files.
- Preserve pinned reference revisions and source locations for implemented rules. Preserve GPL-3.0-or-later and existing third-party/file-level notices. Do not imply Blizzard permission or rights transfer.
- Describe fidelity and prototype reductions accurately; the Den camp and cave are authored adaptations, not original campaign maps.

## Verify changes

For default-profile changes, run:

```sh
./dev build
./dev test
```

For optional Den changes, when its local inputs have been prepared, also run:

```sh
./dev den build
./dev den test
```

If optional checks cannot run because inputs or tools are unavailable, report that and the checks completed for the default profile. Do not prepare private game inputs solely to work on the default profile.
