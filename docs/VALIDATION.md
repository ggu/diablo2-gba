# Validation

This record separates measured results inherited from the source snapshot from checks performed against the public source export. Emulated GBA results are not physical hardware results, and neither automated tests nor one playthrough establish full original-game fidelity.

## Reproducible checks

For the default original-art profile, run:

```sh
./dev doctor
./dev build
./dev test
```

For the optional Den profile, first follow [Development](DEVELOPMENT.md#optional-den-profile), then run:

```sh
./dev den prepare
./dev den build
./dev den test
```

To play either profile, install mGBA as described in [Development](DEVELOPMENT.md#install-the-tools) and run `./dev play diablo2` or `./dev den play`. Emulator results do not establish physical GBA compatibility.

## Inherited Den measurements

The following figures were measured on the original local source snapshot before this public export; they are not results from the clean-clone checks below.

- The latest brighter, magnified Den ROM averaged **29.26 fps** over the full quest on the emulated GBA CPU.
- The same 861-event, 3,664-tick controller quest completed alive, including loot, equipment, and Akara's reward. All five ARM state checkpoints matched the host; four captured ARM frames matched the host renderer pixel for pixel.
- The ROM was 1,048,576 bytes, SHA-256 `e6abd4bf9e5f4a8a5b60ff343630149bd359d323cfdb9214bcd9d2b2e83a2ef5`. ARM text/assets used 536,700 bytes; data used 4 bytes; BSS used 204,520 bytes; IWRAM used 17,400 bytes. Both linker memory guards passed.
- Interval rates were 28.81, 29.80, 29.35, and 28.99 fps. The worst tick took 4,789,379 cycles (about 0.29 seconds), including area setup.
- Fresh-process SRAM loading preserved all quest and character fields and all 1,344 gear/ground-item bytes. The 10,916-byte save payload was unchanged.
- The snapshot had sanitizer-backed inventory/save fixtures, a controller-only full-quest replay, ARM/host state and pixel comparisons, and a fresh-process SRAM reload. Only visible entities entered the depth sort; hot drawing code ran in IWRAM, the terrain cache supplied the bounded 2× window expansion, and cached HUD bands used direct DMA copies.
- No physical GBA SP test is claimed.

The renderer tests covered transparent 2× sprite expansion at odd and even coordinates and all clipping edges, opaque 1×/2× camera windows, and 30,000 randomized renderer inputs. These results describe the previously verified snapshot; the public-export checks below are tracked separately.

## Public-export checks (2026-10-22)

A clean source export without game inputs, generated headers, or external reference checkouts passed `./dev doctor`, `./dev build`, and `./dev test` on macOS. The checks include the 30,000-frame default game/renderer run under ASan/UBSan, fixture-only Den inventory/save rules, 1×/2× drawing bounds, and Python importer/download tests. The default ROM was 65,536 bytes, SHA-256 `7d4b3deb4c12310a131c1a7c06f17d25a6b1b0b2346efc8a3120406ddead451a`. A 30-tick mGBA smoke check also produced a valid SRAM record.

With ignored local Den inputs, `./dev den build` reproduced the latest snapshot ROM byte for byte, and `./dev den test` passed the controller quest and renderer checks. The standalone replay tools then matched all five ARM/host state checkpoints, all four presented framebuffers, and the fresh-process quest/inventory save. The full replay again averaged 29.26 fps over 3,664 ticks.

The preparation workflow also completed with freshly fetched pinned readers, a freshly built StormLib, and the cached, hash-verified demo installer. It extracted six MPQs and the three item-table inputs, regenerated the cave/item/town headers, and reported `prepared: true`. All three headers matched the original local bundle byte for byte. The 15 Python checks include a regression for the missing-table preparation path and the order of extraction and packing.

To reproduce the optional ARM, pixel, and save checks after preparation:

```sh
./dev den build
./dev den test
python3 tools/d2_profile.py
python3 tools/d2_visual_compare.py
python3 tools/d2_save_check.py
```

These extra checks require mGBA. Their captures, traces, saves, and reports remain under ignored `build/`. The source guard also passed, including a rejection check for a staged Den asset header placed outside the ignored directories. GitHub Actions repeats the asset-free build and tests on Ubuntu 24.04.

## Known limits

The default game is an original-art prototype with two generated zones, a small authored monster/item system, and reduced mechanics. The Den slice is a separate educational adaptation with a fixed authored cave and compact camp. See [the default game guide](../diablo2/README.md) and [the Den guide](D2-DEN.md) for implemented features and omissions.
