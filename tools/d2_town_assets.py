#!/usr/bin/env python3
"""Prepare a private Rogue Encampment asset bundle from user-owned D2 MPQs.

The generated header and previews are private derived assets; this repository
contains only the importer, not Blizzard data.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import re
import struct
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import d2_import  # noqa: E402

OUT = ROOT / ".local/d2/generated"
HEADER = OUT / "d2_town_assets.h"

TOWN_MAPS = tuple(rf"data\global\tiles\ACT1\TOWN\town{edge}1.ds1" for edge in "NESW")
TOWN_TILES = (
    r"data\global\tiles\ACT1\TOWN\floor.dt1",
    r"data\global\tiles\ACT1\OUTDOORS\blank.dt1",
    r"data\global\tiles\ACT1\BARRACKS\InvisWal.dt1",
    r"data\global\tiles\ACT1\BARRACKS\Warp.dt1",
)
TOWN_EXCEL = (r"data\global\excel\LvlPrest.txt", r"data\global\excel\LvlTypes.txt")
AKARA_COF = {
    "neutral": (r"data\global\monsters\PS\COF\PSNUHTH.cof", "NU"),
    # The complete archive may contain a dedicated S1/gesture mode. The
    # demo's MPQ includes COFs for PS but not all corresponding DCCs.
    "talk": (r"data\global\monsters\PS\COF\PSS1HTH.cof", "S1"),
}
PALETTE = r"data\global\palette\ACT1\pal.dat"
ITEM_ICONS = ((1, "hax"), (2, "buc"), (3, "cap"), (4, "qlt"), (5, "lbt"),
              (6, "lgl"), (7, "lbl"), (8, "clb"), (9, "scm"))
BONFIRE_COF = r"data\global\objects\RB\COF\RBNUHTH.cof"
BONFIRE_DCC = r"data\global\objects\RB\TR\RBTRLITNUHTH.dcc"


def _table_rows(data: bytes) -> list[dict[str, str]]:
    return list(csv.DictReader(io.StringIO(data.decode("cp1252")), delimiter="\t"))


def town_dt1_members(archives: list[Path]) -> list[str]:
    """Read the original Act I Town LvlTypes/LvlPrest DT1 mask."""
    prest_archive, prest_raw = probe_member(archives, TOWN_EXCEL[0])
    types_archive, types_raw = probe_member(archives, TOWN_EXCEL[1])
    if prest_raw is None or types_raw is None:
        return list(TOWN_TILES)
    prest = next((row for row in _table_rows(prest_raw) if row.get("Def") == "1"), None)
    levels = next((row for row in _table_rows(types_raw) if row.get("Id") == "1"), None)
    if prest is None or levels is None:
        return list(TOWN_TILES)
    mask = int(prest.get("Dt1Mask", "0"))
    members = []
    for index in range(32):
        filename = levels.get(f"File {index + 1}", "")
        if mask >> index & 1 and filename not in ("", "0"):
            members.append("data\\global\\tiles\\" + filename.replace("/", "\\"))
    # The game's preset-room loader appends these support tiles regardless of
    # the mask; preserve its order and skip duplicates.
    members.extend(TOWN_TILES[1:])
    if TOWN_TILES[0] not in members:
        members.insert(0, TOWN_TILES[0])
    unique = {}
    for member in members:
        unique.setdefault(member.casefold(), member)
    return list(unique.values())


def probe_member(archives: list[Path], member: str) -> tuple[Path | None, bytes | None]:
    for archive in archives:
        try:
            return archive, d2_import.extract_mpq_member(archive, member)
        except FileNotFoundError:
            continue
    return None, None


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def source_report(archives: list[Path]) -> dict:
    tile_members = town_dt1_members(archives)
    icon_members = [rf"data\global\items\inv{code}.dc6" for _icon_id, code in ITEM_ICONS]
    members = [*TOWN_MAPS, *tile_members, *TOWN_EXCEL, PALETTE, BONFIRE_COF, BONFIRE_DCC,
               *(path for path, _ in AKARA_COF.values()), *icon_members]
    members = list(dict.fromkeys(members))
    report = {"archives": [str(p) for p in archives], "reader_commit": d2_import.REF_COMMIT,
              "source_fingerprints": {str(p): sha256_file(p) if p.is_file() else None for p in archives},
              "references": {
                  "town_map_members": "references/d2decomped/components/game/gamedata_load.cpp:55-140",
                  "town_map_test": "references/d2decomped/tests/test_ds1.cpp:61-78",
                  "town_floor_test": "references/d2decomped/tests/test_dt1.cpp:47-60",
                  "akara_monster_id": "references/d2decomped/components/rules/monster_ids.hpp:11",
              }, "members": {}}
    for member in members:
        archive, data = probe_member(archives, member)
        report["members"][member] = {"present": data is not None,
                                     "archive": str(archive) if archive else None,
                                     "size_bytes": len(data) if data is not None else 0,
                                     "sha256": hashlib.sha256(data).hexdigest() if data is not None else None}
    report["town_map_available"] = any(report["members"][p]["present"] for p in TOWN_MAPS)
    report["town_floor_available"] = report["members"][TOWN_TILES[0]]["present"]
    report["akara_neutral_available"] = report["members"][AKARA_COF["neutral"][0]]["present"]
    report["akara_talk_available"] = report["members"][AKARA_COF["talk"][0]]["present"]
    report["town_dt1_count"] = sum(report["members"][p]["present"] for p in tile_members)
    report["campfire_available"] = all(report["members"][p]["present"] for p in (BONFIRE_COF, BONFIRE_DCC))
    report["item_icon_count"] = sum(report["members"][p]["present"] for p in icon_members)
    report["core_ready"] = all((report["town_map_available"], report["town_floor_available"],
                                report["akara_neutral_available"], report["members"][PALETTE]["present"]))
    report["ready"] = report["core_ready"] and report["item_icon_count"] == len(ITEM_ICONS)
    report["partial_ready"] = report["ready"] and not report["akara_talk_available"]
    return report


def parse_shared_palette() -> list[int]:
    den = (OUT / "d2_den_assets.h").read_text(encoding="ascii")
    match = re.search(r"d2_den_palette\[224\]\s*=\s*\{(.*?)\};", den, re.S)
    if not match:
        raise RuntimeError("existing d2_den_assets.h has no 224-colour shared world palette")
    values = [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{4})u", match.group(1))]
    if len(values) != 224:
        raise RuntimeError("existing D2 world palette is not 224 entries")
    return values


def map_palette(source: list[int], shared: list[int]) -> list[int]:
    """Map original source colors into shared world indices 32..255."""
    mapping = [0] * 256
    for i in range(1, 256):
        color = source[i]
        r, g, b = (color >> 10) & 31, (color >> 5) & 31, color & 31
        best = min(range(len(shared)), key=lambda j:
            ((r - ((shared[j] >> 10) & 31)) ** 2
             + (g - ((shared[j] >> 5) & 31)) ** 2
             + (b - (shared[j] & 31)) ** 2))
        mapping[i] = best + 32
    return mapping


def serialize_bytes(symbol: str, pixels: bytes) -> list[str]:
    lines = [f"static const u8 {symbol}[{len(pixels)}] = {{"]
    lines.extend("    " + ", ".join(str(x) for x in pixels[pos:pos + 24]) + ","
                 for pos in range(0, len(pixels), 24))
    lines.append("};")
    return lines


def actor_animation(archive: Path, cof_member: str, mode: str, index_map: list[int]) -> dict:
    cof = d2_import.cof_info(d2_import.extract_mpq_member(archive, cof_member))
    slots = ("HD", "TR", "LG", "RA", "LA", "RH", "LH", "SH", "S1", "S2", "S3", "S4",
             "S5", "S6", "S7", "S8")
    decoded: dict[int, list[dict]] = {}
    for layer in cof["layers"]:
        if layer["type"] >= len(slots):
            raise ValueError(f"unknown Akara COF layer {layer['type']}")
        slot = slots[layer["type"]]
        variation = "BUC" if slot == "SH" else "LIT"
        stem = f"PS{slot}{variation}{mode}HTH"
        member = rf"data\global\monsters\PS\{slot}\{stem}.dcc"
        raw = d2_import.extract_mpq_member(archive, member)
        dirs, per, frames = d2_import.dcc_frames(raw)
        if dirs < cof["directions"] or per < cof["frames"]:
            raise ValueError(f"DCC does not cover COF layer {member}")
        decoded[layer["type"]] = [frames[d * per + f] for d in range(cof["directions"])
                                  for f in range(cof["frames"])]
    animation = d2_import._compose_frames(cof, decoded)
    for frame in animation["images"]:
        frame["pixels"] = bytes(0 if p == 0 else index_map[p] for p in frame["pixels"])
    return animation


def ground_icon(icon: dict) -> dict:
    """Pre-rasterize the loot sprite using den_item_icon's exact mapping."""
    width, height = 9, 15
    source_width, source_height = icon["width"], icon["height"]
    source = icon["pixels"]
    pixels = bytes(source[(y * source_height // height) * source_width +
                          (x * source_width // width)]
                   for y in range(height) for x in range(width))
    return {"width": width, "height": height, "x": -4, "y": -12,
            "pixels": pixels}


def build(archives: list[Path]) -> dict:
    report = source_report(archives)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "town-source-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if not report["core_ready"]:
        return report

    chosen_map = next(p for p in TOWN_MAPS if report["members"][p]["present"])
    map_archive = Path(report["members"][chosen_map]["archive"])
    ds1_head = d2_import.extract_mpq_member(map_archive, chosen_map)
    ds1_version, ds1_width, ds1_height = struct.unpack_from("<3i", ds1_head)
    palette_archive = Path(report["members"][PALETTE]["archive"])
    palette = d2_import.rgb555_palette(d2_import.extract_mpq_member(palette_archive, PALETTE))
    shared = parse_shared_palette()
    mapping = map_palette(palette, shared)

    floor_data: list[tuple[str, int, int, int, int, int, int, bytes]] = []
    prop_data: list[tuple[str, int, int, int, int, int, int, bytes]] = []
    tile_members = town_dt1_members(archives)
    for member in tile_members:
        if not report["members"][member]["present"]:
            continue
        archive = Path(report["members"][member]["archive"])
        per_member = 0
        for tile in d2_import.dt1_tiles(d2_import.extract_mpq_member(archive, member)):
            if tile["width"] == 0 or tile["height"] == 0 or not any(tile["pixels"]):
                continue
            is_floor_archive = member.casefold().endswith(r"\town\floor.dt1")
            if is_floor_archive != (tile["type"] == 0):
                continue
            w, h, pixels = d2_import.downsample_indexed(tile["pixels"], tile["width"], tile["height"], 4)
            pixels = bytes(0 if p == 0 else mapping[p] for p in pixels)
            record = (member.rsplit("\\", 1)[-1].replace(".", "_") + f"_{tile['style']}_{tile['sequence']}",
                      w, h, tile["type"], tile["style"], tile["sequence"], tile["y_shift"] // 4, pixels)
            (floor_data if is_floor_archive else prop_data).append(record)
            per_member += 1
            if per_member == (24 if is_floor_archive else 4):
                break
    if not floor_data:
        raise ValueError("town DT1 archives yielded no painted terrain tiles")
    floor_data = floor_data[:24]
    prop_data = prop_data[:24]

    animations = {}
    for name, (member, mode) in AKARA_COF.items():
        if not report["members"][member]["present"]:
            continue
        archive = Path(report["members"][member]["archive"])
        animations[name] = actor_animation(archive, member, mode, mapping)

    icons = {}
    for icon_id, code in ITEM_ICONS:
        member = rf"data\global\items\inv{code}.dc6"
        if not report["members"][member]["present"]:
            continue
        archive = Path(report["members"][member]["archive"])
        dirs, per, frames = d2_import.dc6_frames(d2_import.extract_mpq_member(archive, member))
        if dirs < 1 or per < 1:
            continue
        frame = frames[0]
        w, h, pixels = d2_import.downsample_indexed(frame["pixels"], frame["width"], frame["height"], 2)
        pixels = bytes(0 if p == 0 else mapping[p] for p in pixels)
        icons[icon_id] = {"width": w, "height": h, "x": frame["x"] // 2,
                          "y": frame["y"] // 2, "pixels": pixels}

    campfire = None
    if report["campfire_available"]:
        archive = Path(report["members"][BONFIRE_DCC]["archive"])
        dirs, per, frames = d2_import.dcc_frames(d2_import.extract_mpq_member(archive, BONFIRE_DCC))
        cof_archive = Path(report["members"][BONFIRE_COF]["archive"])
        cof = d2_import.cof_info(d2_import.extract_mpq_member(cof_archive, BONFIRE_COF))
        if dirs < 1 or per < cof["frames"]:
            raise ValueError("RogueBonfire DCC frames do not cover its COF")
        campfire = []
        for frame in frames[:cof["frames"]]:
            w, h, pixels = d2_import.downsample_indexed(frame["pixels"], frame["width"], frame["height"], 4)
            pixels = bytes(0 if p == 0 else mapping[p] for p in pixels)
            campfire.append({"width": w, "height": h, "x": frame["x"] // 4,
                             "y": frame["y"] // 4, "pixels": pixels})

    lines = ["/* Generated privately from user-provided D2 archives.",
             f" * D2Decomp readers pinned at {d2_import.REF_COMMIT}.",
             " * Keep this derived data under .local/d2/generated/. */",
             "#ifndef D2_TOWN_ASSETS_H", "#define D2_TOWN_ASSETS_H", '#include "gba.h"',
             '#include "d2_den_assets.h"',
             "typedef struct { u16 w, h; s16 x, y; u8 type, style, sequence; const u8 *pixels; } D2TownTile;", ""]
    for i, (name, w, h, _type, _style, _sequence, _y, pixels) in enumerate(floor_data):
        lines.extend(serialize_bytes(f"d2_town_tile_pixels_{i}", pixels))
    lines.append(f"static const u16 d2_town_ground_tile_count = {len(floor_data)}u;")
    lines.append(f"static const D2TownTile d2_town_ground_tiles[{len(floor_data)}] = {{")
    for i, (_name, w, h, kind, style, sequence, y, _pixels) in enumerate(floor_data):
        lines.append(f"    {{{w}u, {h}u, 0, {y}, {kind}, {style}, {sequence}, d2_town_tile_pixels_{i}}},")
    lines.extend(["};", ""])
    for i, (name, w, h, _type, _style, _sequence, _y, pixels) in enumerate(prop_data):
        lines.extend(serialize_bytes(f"d2_town_prop_pixels_{i}", pixels))
    lines.append(f"static const u16 d2_town_static_prop_count = {len(prop_data)}u;")
    lines.append(f"static const D2TownTile d2_town_static_props[{max(1, len(prop_data))}] = {{")
    for i, (_name, w, h, kind, style, sequence, y, _pixels) in enumerate(prop_data):
        lines.append(f"    {{{w}u, {h}u, 0, {y}, {kind}, {style}, {sequence}, d2_town_prop_pixels_{i}}},")
    if not prop_data:
        lines.append("    {0u, 0u, 0, 0, 0, 0, 0, 0},")
    lines.extend(["};", ""])
    for animation_name, animation in animations.items():
        imgs = animation["images"]
        for i, frame in enumerate(imgs):
            lines.extend(serialize_bytes(f"d2_akara_{animation_name}_pixels_{i}", frame["pixels"]))
        lines.append(f"static const D2SpriteFrame d2_akara_{animation_name}_frames[{len(imgs)}] = {{")
        for i, frame in enumerate(imgs):
            lines.append(f"    {{{frame['width']}u, {frame['height']}u, {frame['x']}, {frame['y']}, d2_akara_{animation_name}_pixels_{i}}},")
        lines.extend(["};", f"static const D2Animation d2_town_akara_{animation_name} = "
                      f"{{{animation['frames']}u, 8u, d2_akara_{animation_name}_frames}};", ""])
    if "talk" not in animations:
        # Keep the renderer contract explicit: talk art is absent, so callers
        # can fall back to the real neutral frames without displaying a fake.
        lines.append("static const D2Animation d2_town_akara_talk = {0u, 8u, 0};")
    if "neutral" in animations and "talk" not in animations:
        lines.append("static const D2Animation d2_town_akara[2] = {d2_town_akara_neutral, {0u, 8u, 0}};")
    elif "neutral" in animations and "talk" in animations:
        lines.append("static const D2Animation d2_town_akara[2] = {d2_town_akara_neutral, d2_town_akara_talk};")
    lines.extend(["", "/* Item icon indices are the stable base IDs from tools/d2_town_assets.py. */"])
    for icon_id, _code in ITEM_ICONS:
        icon = icons.get(icon_id)
        if icon:
            lines.extend(serialize_bytes(f"d2_item_icon_pixels_{icon_id}", icon["pixels"]))
    ground_icons = {icon_id: ground_icon(icons[icon_id]) for icon_id, _code in ITEM_ICONS
                    if icon_id in icons}
    for icon_id, icon in ground_icons.items():
        lines.extend(serialize_bytes(f"d2_item_ground_icon_pixels_{icon_id}", icon["pixels"]))
    lines.append("static const D2SpriteFrame d2_item_icons[10] = {")
    lines.append("    {0u, 0u, 0, 0, 0},")
    for icon_id, _code in ITEM_ICONS:
        icon = icons.get(icon_id)
        if icon:
            lines.append(f"    {{{icon['width']}u, {icon['height']}u, {icon['x']}, {icon['y']}, d2_item_icon_pixels_{icon_id}}},")
        else:
            lines.append("    {0u, 0u, 0, 0, 0},")
    lines.append("};")
    lines.append("static const D2SpriteFrame d2_item_ground_icons[10] = {")
    lines.append("    {0u, 0u, 0, 0, 0},")
    for icon_id, _code in ITEM_ICONS:
        ground = ground_icons.get(icon_id)
        if ground:
            lines.append(f"    {{{ground['width']}u, {ground['height']}u, {ground['x']}, {ground['y']}, d2_item_ground_icon_pixels_{icon_id}}},")
        else:
            lines.append("    {0u, 0u, 0, 0, 0},")
    lines.append("};")
    if campfire:
        for i, frame in enumerate(campfire):
            lines.extend(serialize_bytes(f"d2_town_campfire_pixels_{i}", frame["pixels"]))
        lines.append(f"static const D2SpriteFrame d2_town_campfire_frames[{len(campfire)}] = {{")
        for i, frame in enumerate(campfire):
            lines.append(f"    {{{frame['width']}u, {frame['height']}u, {frame['x']}, {frame['y']}, d2_town_campfire_pixels_{i}}},")
        lines.extend(["};", f"static const D2Animation d2_town_campfire = {{{len(campfire)}u, 1u, d2_town_campfire_frames}};"])
    lines.extend(["#endif /* D2_TOWN_ASSETS_H */", ""])
    with tempfile.NamedTemporaryFile("w", encoding="ascii", dir=OUT, delete=False) as header_tmp:
        header_tmp.write("\n".join(lines))
        temp_header = Path(header_tmp.name)
    temp_header.replace(HEADER)
    report.update({"header": str(HEADER), "town_map": chosen_map,
                   "town_map_dimensions": [ds1_width + 1, ds1_height + 1],
                   "town_map_version": ds1_version,
                   "town_map_layout_mode": "authored_compact",
                   "town_map_layout_note": "The canonical DS1 map is present and versioned in this report. The GBA header exports compact source tiles and the runtime places them in a reduced authored town scene; it does not rasterize the full DS1 layers.",
                   "ground_tile_count": len(floor_data), "static_prop_count": len(prop_data),
                   "item_icon_count": len(icons),
                   "item_ground_icon_count": len(icons),
                   "campfire_frames": len(campfire) if campfire else 0,
                   "akara_frames": {name: anim["frames"] for name, anim in animations.items()}})
    (OUT / "town-source-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    preview_palette = []
    for color in shared:
        r = (color & 31) * 255 // 31
        g = ((color >> 5) & 31) * 255 // 31
        b = ((color >> 10) & 31) * 255 // 31
        preview_palette.append((r, g, b, 255))
    preview_palette = [(0, 0, 0, 0)] + preview_palette + [(0, 0, 0, 255)] * 31
    preview_dir = OUT / "previews"
    preview_dir.mkdir(parents=True, exist_ok=True)
    if "neutral" in animations:
        frame = animations["neutral"]["images"][0]
        d2_import.write_png(preview_dir / "town_akara_neutral.png", frame["width"], frame["height"],
                            frame["pixels"], preview_palette)
    for i, (_name, w, h, _type, _style, _sequence, _y, pixels) in enumerate(floor_data):
        d2_import.write_png(preview_dir / f"town_floor_{i:02d}.png", w, h, pixels, preview_palette)
    for i, (_name, w, h, _type, _style, _sequence, _y, pixels) in enumerate(prop_data):
        d2_import.write_png(preview_dir / f"town_prop_{i:02d}.png", w, h, pixels, preview_palette)
    for icon_id, icon in icons.items():
        d2_import.write_png(preview_dir / f"town_item_{icon_id:02d}.png", icon["width"], icon["height"],
                            icon["pixels"], preview_palette)
    if campfire:
        frame = campfire[0]
        d2_import.write_png(preview_dir / "town_campfire.png", frame["width"], frame["height"],
                            frame["pixels"], preview_palette)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mpq", type=Path, action="append",
                        help="user-owned MPQ archive; repeat to search an archive set")
    parser.add_argument("--probe-only", action="store_true", help="only report canonical source availability")
    args = parser.parse_args()
    try:
        archives = args.mpq or sorted((ROOT / ".local/d2/source").glob("*.mpq"))
        if not archives:
            parser.error("provide --mpq ARCHIVE or place user-owned MPQs in .local/d2/source")
        report = source_report(archives) if args.probe_only else build(archives)
    except Exception as error:
        print(json.dumps({"error": str(error)}, indent=2), file=sys.stderr)
        return 1
    print(json.dumps(report, indent=2))
    return 0 if report.get("core_ready", report.get("ready", False)) else 2


if __name__ == "__main__":
    raise SystemExit(main())
