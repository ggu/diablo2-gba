#!/usr/bin/env python3
"""Convert user-supplied Diablo II assets into private GBA-ready data.

This tool ships no Blizzard content. Source archives and every derived asset
are expected to stay under .local/d2/ (ignored by Git). It uses the pinned
D2Decomp readers in references/d2decomped for binary format parsing.

Copyright (C) 2026 blizzard-gba contributors
SPDX-License-Identifier: GPL-3.0-or-later
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import math
import shutil
import subprocess
import struct
import sys
import zlib
from pathlib import Path
from typing import Iterable

ROOT = Path(__file__).resolve().parents[1]
REF = ROOT / "references/d2decomped"
REF_COMMIT = "f344332ce4b2e42e54ca3b5d7c8e480f49d823ee"
DEFAULT_OUT = ROOT / ".local/d2/generated"
DEFAULT_SOURCE = ROOT / ".local/d2/extracted"


def dc6_frames(data: bytes) -> tuple[int, int, list[dict]]:
    """Decode DC6 RLE frames into indexed pixels (zero is transparent)."""
    if len(data) < 24:
        raise ValueError("DC6: truncated file header")
    version, _sub, _encoding, _term, dirs, per_dir = struct.unpack_from("<6I", data)
    if version != 6:
        raise ValueError(f"DC6: unsupported version {version}")
    count = dirs * per_dir
    if count > 8192 or 24 + count * 4 > len(data):
        raise ValueError("DC6: invalid frame pointer table")
    frames = []
    for index in range(count):
        off = struct.unpack_from("<I", data, 24 + index * 4)[0]
        if off + 32 > len(data):
            raise ValueError("DC6: frame header outside file")
        flip, width, height, ox, oy, _unk, _next, rle_len = struct.unpack_from("<8I", data, off)
        if width > 2048 or height > 2048 or rle_len > len(data):
            raise ValueError("DC6: unreasonable frame dimensions or data length")
        start, end = off + 32, off + 32 + rle_len
        if end > len(data):
            raise ValueError("DC6: RLE data outside file")
        pix = bytearray(width * height)
        x, y, pos = 0, (0 if flip else height - 1), start
        while pos < end:
            token = data[pos]
            pos += 1
            if token == 0x80:
                x = 0
                y += 1 if flip else -1
            elif token & 0x80:
                x += token & 0x7F
            else:
                if pos + token > end or y < 0 or y >= height or x + token > width:
                    raise ValueError("DC6: malformed RLE run")
                pix[y * width + x:y * width + x + token] = data[pos:pos + token]
                x += token
                pos += token
        frames.append({"width": width, "height": height, "x": _signed32(ox),
                       "y": _signed32(oy), "pixels": bytes(pix)})
    return dirs, per_dir, frames


def _signed32(value: int) -> int:
    return value - 0x100000000 if value & 0x80000000 else value


def cof_info(data: bytes) -> dict:
    """Read the COF layer table and per-direction/frame draw-order recipe."""
    if len(data) < 28:
        raise ValueError("COF: truncated header")
    layers, frames, dirs = data[0], data[1], data[2]
    speed = data[24]
    off = 28
    if off + layers * 9 + frames > len(data):
        raise ValueError("COF: truncated layer/event table")
    layer_defs = []
    for _ in range(layers):
        row = data[off:off + 9]
        layer_defs.append({"type": row[0], "shadow": row[1], "selectable": bool(row[2]),
                           "transparent": bool(row[3]), "effect": row[4],
                           "weapon_class": row[5:9].split(b"\0", 1)[0].decode("ascii", "replace")})
        off += 9
    events = list(data[off:off + frames])
    off += frames
    need = dirs * frames * layers
    if off + need > len(data):
        raise ValueError("COF: truncated draw-order table")
    return {"layers": layer_defs, "frames": frames, "directions": dirs,
            "speed": speed, "events": events,
            "priority": [list(data[off+i*layers:off+(i+1)*layers]) for i in range(dirs*frames)]}


def dcc_frames(data: bytes) -> tuple[int, int, list[dict]]:
    """Decode DCC using the pinned, real-asset-tested D2Decomp C++ reader."""
    compiler = shutil.which("c++") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("DCC decoding requires a C++20 compiler")
    header = REF / "components/dcc/dcc.hpp"
    if not header.is_file():
        raise RuntimeError(f"missing pinned DCC reader: {header}")
    cache = ROOT / ".local/d2/cache"
    cache.mkdir(parents=True, exist_ok=True)
    source = cache / "decode_dcc.cpp"
    binary = cache / "decode_dcc"
    if not binary.exists() or binary.stat().st_mtime < max(header.stat().st_mtime, Path(__file__).stat().st_mtime):
        source.write_text(r'''#include <dcc.hpp>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
int main() {
  std::vector<char> raw((std::istreambuf_iterator<char>(std::cin)), {});
  std::vector<std::byte> bytes(raw.size());
  for (std::size_t i=0; i<raw.size(); ++i) bytes[i]=std::byte(static_cast<unsigned char>(raw[i]));
  try {
    d2d::dcc::Sprite sprite(bytes);
    const std::uint32_t magic=0x31434344u;
    auto put=[&](auto value){ std::cout.write(reinterpret_cast<const char*>(&value), sizeof(value)); };
    const std::uint32_t dirs=sprite.directions(); put(magic); put(dirs);
    const std::int32_t per=sprite.frames_per_direction(); put(per);
    for (const auto& f : sprite.frames()) {
      put(f.width); put(f.height); put(f.x_offset); put(f.y_offset - f.height + 1);
      std::uint32_t n=f.pixels.size(); put(n);
      std::cout.write(reinterpret_cast<const char*>(f.pixels.data()), n);
    }
  } catch (const std::exception& e) { std::cerr << e.what(); return 2; }
}
''', encoding="ascii")
        subprocess.run([compiler, "-std=c++20", "-O2", "-I", str(REF / "components/dcc"),
                        str(source), "-o", str(binary)], check=True, capture_output=True, text=True)
    proc = subprocess.run([str(binary)], input=data, check=True, capture_output=True)
    out = memoryview(proc.stdout)
    if len(out) < 12:
        raise ValueError("DCC decoder returned truncated output")
    magic, dirs, per_dir = struct.unpack_from("<IIi", out, 0)
    if magic != 0x31434344 or dirs * per_dir > 8192:
        raise ValueError("invalid DCC decoder output")
    pos, frames = 12, []
    for _ in range(dirs * per_dir):
        if pos + 20 > len(out):
            raise ValueError("truncated DCC frame output")
        width, height, ox, oy, size = struct.unpack_from("<4iI", out, pos)
        pos += 20
        if width < 0 or height < 0 or width * height != size or pos + size > len(out):
            raise ValueError("invalid DCC frame output dimensions")
        frames.append({"width": width, "height": height, "x": ox, "y": oy,
                       "pixels": bytes(out[pos:pos + size])})
        pos += size
    if pos != len(out):
        raise ValueError("unexpected trailing bytes from DCC decoder")
    return dirs, per_dir, frames


def dt1_tiles(data: bytes) -> list[dict]:
    """Decode DT1 tile pixels using the pinned D2Decomp reader."""
    compiler = shutil.which("c++") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("DT1 decoding requires a C++20 compiler")
    header = REF / "components/dt1/dt1.hpp"
    cache = ROOT / ".local/d2/cache"
    cache.mkdir(parents=True, exist_ok=True)
    source, binary = cache / "decode_dt1.cpp", cache / "decode_dt1"
    if not binary.exists() or binary.stat().st_mtime < max(header.stat().st_mtime, Path(__file__).stat().st_mtime):
        source.write_text(r'''#include <dt1.hpp>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <vector>
int main() {
  std::vector<char> raw((std::istreambuf_iterator<char>(std::cin)), {});
  std::vector<std::byte> bytes(raw.size());
  for (std::size_t i=0; i<raw.size(); ++i) bytes[i]=std::byte(static_cast<unsigned char>(raw[i]));
  try {
    d2d::dt1::Archive archive(bytes);
    auto put=[&](auto value){ std::cout.write(reinterpret_cast<const char*>(&value), sizeof(value)); };
    const std::uint32_t magic=0x31544444u, count=archive.tiles().size(); put(magic); put(count);
    for (const auto& t : archive.tiles()) {
      put(t.width); put(t.height); put(t.y_shift); put(t.type); put(t.style); put(t.sequence);
      const std::uint32_t n=t.pixels.size(); put(n);
      std::cout.write(reinterpret_cast<const char*>(t.pixels.data()), n);
    }
  } catch (const std::exception& e) { std::cerr << e.what(); return 2; }
}
''', encoding="ascii")
        subprocess.run([compiler, "-std=c++20", "-O2", "-I", str(REF / "components/dt1"),
                        str(source), "-o", str(binary)], check=True, capture_output=True, text=True)
    proc = subprocess.run([str(binary)], input=data, check=True, capture_output=True)
    out = memoryview(proc.stdout)
    if len(out) < 8:
        raise ValueError("DT1 decoder returned truncated output")
    magic, count = struct.unpack_from("<II", out, 0)
    if magic != 0x31544444 or count > 100000:
        raise ValueError("invalid DT1 decoder output")
    pos, tiles = 8, []
    for _ in range(count):
        if pos + 28 > len(out):
            raise ValueError("truncated DT1 tile output")
        width, height, y_shift, kind, style, sequence, size = struct.unpack_from("<6iI", out, pos)
        pos += 28
        if width < 0 or abs(height) > 4096 or width > 4096 or size != width * abs(height) or pos + size > len(out):
            raise ValueError("invalid DT1 tile dimensions")
        tiles.append({"width": width, "height": abs(height), "y_shift": y_shift,
                      "type": kind, "style": style, "sequence": sequence,
                      "pixels": bytes(out[pos:pos + size])})
        pos += size
    if pos != len(out):
        raise ValueError("unexpected trailing bytes from DT1 decoder")
    return tiles


def downsample_indexed(pixels: bytes, width: int, height: int,
                       scale: int = 4) -> tuple[int, int, bytes]:
    """Nearest-neighbour shrink preserving palette indices and transparency."""
    if scale < 1 or width * height != len(pixels):
        raise ValueError("invalid indexed image or scale")
    out_w, out_h = max(1, math.ceil(width / scale)), max(1, math.ceil(height / scale))
    out = bytearray(out_w * out_h)
    for y in range(out_h):
        sy = min(height - 1, y * scale + scale // 2)
        for x in range(out_w):
            sx = min(width - 1, x * scale + scale // 2)
            out[y * out_w + x] = pixels[sy * width + sx]
    return out_w, out_h, bytes(out)


def floor_buffer(tile: dict, index_map: list[int] | None = None) -> bytes:
    """Fit a DT1 floor into the renderer's fixed 40x20 palette-index diamond."""
    width, height, source = tile["width"], tile["height"], tile["pixels"]
    result = bytearray(40 * 20)
    for y in range(20):
        sy = y * 4 + 2
        if sy >= height:
            continue
        for x in range(40):
            sx = x * 4 + 2
            if sx < width:
                index = source[sy * width + sx]
                result[y * 40 + x] = index_map[index] if index_map is not None else index
    return bytes(result)


def _priority_row(direction: int, directions: int) -> int:
    # D2Decomp components/cof/cof.hpp @ pinned reference commit.
    row8 = (1, 3, 5, 7, 0, 2, 4, 6)
    row16 = (2, 6, 10, 14, 0, 4, 8, 12, 1, 3, 5, 7, 9, 11, 13, 15)
    if directions == 8 and direction < 8:
        return row8[direction]
    if directions == 16 and direction < 16:
        return row16[direction]
    if direction >= directions:
        raise ValueError("COF direction row is out of range")
    return direction


def _compose_frames(cof: dict, layers: dict[int, list[dict]], *, output_scale: int = 4) -> dict:
    """Composite one COF animation using its per-direction layer draw order."""
    directions, per_dir = cof["directions"], cof["frames"]
    dcc_dirs = (6, 2, 7, 3, 4, 0, 5, 1)
    out_images = []
    for target_dir in range(8):
        direction = dcc_dirs[target_dir]
        if direction >= directions:
            raise ValueError(f"COF has {directions} directions; cannot select DCC direction {direction}")
        order_dir = _priority_row(direction, directions)
        for frame_index in range(per_dir):
            ordered_types = cof["priority"][order_dir * per_dir + frame_index]
            frame_by_type = {}
            for layer in cof["layers"]:
                kind = layer["type"]
                frames = layers.get(kind)
                if frames is None:
                    continue
                frame_by_type[kind] = frames[direction * per_dir + frame_index]
            visible = [frame_by_type[kind] for kind in ordered_types if kind in frame_by_type]
            if not visible:
                out_images.append({"width": 1, "height": 1, "x": 0, "y": 0, "pixels": b"\0"})
                continue
            left = min(frame["x"] for frame in visible)
            top = min(frame["y"] for frame in visible)
            right = max(frame["x"] + frame["width"] for frame in visible)
            bottom = max(frame["y"] + frame["height"] for frame in visible)
            if right <= left or bottom <= top or right - left > 512 or bottom - top > 512:
                raise ValueError("composite frame has unreasonable bounds")
            canvas = bytearray((right - left) * (bottom - top))
            for kind in ordered_types:
                frame = frame_by_type.get(kind)
                if frame is None:
                    continue
                x0, y0 = frame["x"] - left, frame["y"] - top
                for y in range(frame["height"]):
                    src = y * frame["width"]
                    dst = (y0 + y) * (right - left) + x0
                    row = frame["pixels"][src:src + frame["width"]]
                    for x, pixel in enumerate(row):
                        if pixel:
                            canvas[dst + x] = pixel
            # Sample against the world origin so all frames keep one feet anchor.
            left_scaled, top_scaled = left // output_scale, top // output_scale
            right_scaled = -((-right) // output_scale)
            bottom_scaled = -((-bottom) // output_scale)
            out_w, out_h = max(1, right_scaled - left_scaled), max(1, bottom_scaled - top_scaled)
            reduced = bytearray(out_w * out_h)
            for y in range(out_h):
                source_y = (top_scaled + y) * output_scale + output_scale // 2 - top
                if source_y < 0 or source_y >= bottom - top:
                    continue
                for x in range(out_w):
                    source_x = (left_scaled + x) * output_scale + output_scale // 2 - left
                    if 0 <= source_x < right - left:
                        reduced[y * out_w + x] = canvas[source_y * (right - left) + source_x]
            if not -32768 <= left_scaled <= 32767 or not -32768 <= top_scaled <= 32767:
                raise ValueError("sprite origin does not fit renderer s16 offsets")
            out_images.append({"width": out_w, "height": out_h,
                               "x": left_scaled, "y": top_scaled,
                               "pixels": bytes(reduced)})
    return {"frames": per_dir, "images": out_images}


def _cache_member(archive: Path, member: str) -> bytes:
    relative = Path(member.replace("\\", "/"))
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError(f"unsafe archive member path: {member}")
    output = DEFAULT_SOURCE / relative
    if not output.exists():
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(extract_mpq_member(archive, member))
    return output.read_bytes()


def _actor_animation(code: str, root: str, archive: Path, cof_name: str,
                     action: str) -> dict:
    cof_member = rf"data\global\{root}\{code}\COF\{cof_name}.cof"
    cof = cof_info(_cache_member(archive, cof_member))
    layer_slots = ("HD", "TR", "LG", "RA", "LA", "RH", "LH", "SH",
                   "S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8")
    decoded: dict[int, list[dict]] = {}
    source_dir = "chars" if root == "chars" else "monsters"
    for layer in cof["layers"]:
        kind = layer["type"]
        if kind >= len(layer_slots):
            raise ValueError(f"COF layer type {kind} exceeds known component slots")
        slot = layer_slots[kind]
        weapon_class = layer["weapon_class"].upper()
        # COF layers are a template: the demo's one-handed Barbarian has an
        # axe in RH, a buckler shadow in SH, and no LH item. The original
        # compositor omits an empty hand layer (types 5/6).
        if code == "BA" and slot in ("LH", "LA"):
            continue
        # Zombie/shaman accessory overlays are variant-specific; retain the
        # Fallen's actual MonStats hand axe and buckler below.
        if code != "BA" and slot == "LH":
            continue
        if code not in ("BA", "FA") and kind >= 5:
            continue
        if code == "FA" and kind >= 8:
            continue
        # Shadow components are stored with the BUC variation token; the
        # other component slots use LIT. This naming difference matters for
        # the Barbarian's starting COFs and some monster COFs.
        variation = "BUC" if slot == "SH" else "LIT"
        if code == "BA" and slot == "RH" and weapon_class == "1HS":
            # Demo Barbarian COFs equip the hand axe; weapon DCC names insert
            # the actual item family rather than the generic LIT token.
            stem = f"BARHAXE{action}{weapon_class}"
        elif code == "FA" and slot == "RH":
            stem = f"FARHAXE{action}{weapon_class}"
        else:
            stem = f"{code}{slot}{variation}{action}{weapon_class}"
        member = rf"data\global\{source_dir}\{code}\{slot}\{stem}.dcc"
        raw = _cache_member(archive, member)
        dirs, per_dir, frames = dcc_frames(raw)
        if dirs < cof["directions"] or per_dir < cof["frames"]:
            raise ValueError(f"DCC animation dimensions do not cover COF layer {member}")
        # Directions are indexed by DCC order; strip any DCC-only extra frames.
        decoded[kind] = [frames[direction * per_dir + frame]
                         for direction in range(cof["directions"])
                         for frame in range(cof["frames"])]
    return _compose_frames(cof, decoded)


def import_den_demo(installer: Path, output_dir: Path = DEFAULT_OUT) -> dict:
    """Extract, decode, compose, and emit the original-art Den demo bundle."""
    outer = resolve_mpq(installer)
    chars = ROOT / ".local/d2/cache/File00000023.mpq"
    monsters = ROOT / ".local/d2/cache/File00000025.mpq"
    if not chars.exists():
        chars.write_bytes(extract_mpq_member(outer, "File00000023.mpq"))
    if not monsters.exists():
        monsters.write_bytes(extract_mpq_member(outer, "File00000025.mpq"))
    # Names and modes are the demo's original COF entries. The Barbarian's
    # walk/attack/hit use the starting one-handed axe composite; death is its
    # built-in hand-to-hand corpse sequence, the only death COF in this demo.
    definitions = [
        ("BA", "chars", chars, [("BANU1HS", "NU"), ("BAWL1HS", "WL"),
                                ("BAA11HS", "A1"), ("BAGH1HS", "GH"), ("BADTHTH", "DT")]),
        ("ZM", "monsters", monsters, [("ZMNUHTH", "NU"), ("ZMWLHTH", "WL"),
                                       ("ZMA1HTH", "A1"), ("ZMGHHTH", "GH"), ("ZMDTHTH", "DT")]),
        ("FA", "monsters", monsters, [("FANUHTH", "NU"), ("FAWLHTH", "WL"),
                                       ("FAA1HTH", "A1"), ("FAGHHTH", "GH"), ("FADTHTH", "DT")]),
        ("FS", "monsters", monsters, [("FSNUHTH", "NU"), ("FSWLHTH", "WL"),
                                       ("FSA1HTH", "A1"), ("FSGHHTH", "GH"), ("FSDTHTH", "DT")]),
    ]
    actors = []
    all_images = []
    for code, root, archive, animation_defs in definitions:
        species = []
        for cof_name, action in animation_defs:
            animation = _actor_animation(code, root, archive, cof_name, action)
            species.append(animation)
            all_images.extend(frame["pixels"] for frame in animation["images"])
        actors.append(species)

    palette_raw = _cache_member(monsters, r"data\global\palette\ACT1\pal.dat")
    palette = rgb555_palette(palette_raw)
    dt1_raw = _cache_member(monsters, r"data\global\tiles\ACT1\Caves\cave.dt1")
    dt1 = dt1_tiles(dt1_raw)
    # Style 5, sequence 0 are the eight plain cave floor variants; avoid
    # treating every floor-typed DT1 subtile as a standalone map cell.
    source_floors = [tile for tile in dt1 if tile["type"] == 0 and tile["width"] and tile["height"]
                     and any(tile["pixels"]) and tile["style"] == 5
                     and tile["sequence"] == 0]
    floors = [floor_buffer(tile) for tile in source_floors]
    if not floors:
        raise ValueError("Caves/cave.dt1 contains no decodable floor tiles")
    # Three isolated cave rock/pillar variants from the type-12 object run.
    rocks = []
    for tile_index in (91, 92, 96):
        tile = dt1[tile_index]
        width, height, pixels = downsample_indexed(tile["pixels"], tile["width"],
                                                   tile["height"], 4)
        rocks.append({"width": width, "height": height,
                      "x": -(width // 2), "y": -height,
                      "pixels": pixels, "source_tile": tile_index,
                      "type": tile["type"], "style": tile["style"],
                      "sequence": tile["sequence"]})
    all_images.extend(floors)
    all_images.extend(rock["pixels"] for rock in rocks)
    reduced_palette, index_map = reduce_palette(palette, all_images, 224)
    for species in actors:
        for animation in species:
            for frame in animation["images"]:
                frame["pixels"] = bytes(0 if pixel == 0 else index_map[pixel] + 32
                                         for pixel in frame["pixels"])
    floors = [bytes(0 if pixel == 0 else index_map[pixel] + 32 for pixel in floor)
              for floor in floors]
    for rock in rocks:
        rock["pixels"] = bytes(0 if pixel == 0 else index_map[pixel] + 32
                               for pixel in rock["pixels"])
    output_dir.mkdir(parents=True, exist_ok=True)
    header = output_dir / "d2_den_assets.h"
    emit_header(header, actors, floors, reduced_palette, rocks=rocks)

    # Render small contact sheets from the same final indexed pixels for QA.
    preview_dir = output_dir / "previews"
    preview_dir.mkdir(parents=True, exist_ok=True)
    preview_palette = []
    for color in reduced_palette:
        r = (color & 31) * 255 // 31
        g = ((color >> 5) & 31) * 255 // 31
        b = ((color >> 10) & 31) * 255 // 31
        preview_palette.append((r, g, b, 0 if len(preview_palette) == 0 else 255))
    preview_palette.extend([(0, 0, 0, 255)] * (256 - len(preview_palette)))
    actor_names = ("barbarian", "zombie", "fallen", "shaman")
    anim_names = ("neutral", "walk", "attack", "hit", "death")
    for species_index, species in enumerate(actors):
        for animation_index, animation in enumerate(species):
            frame = animation["images"][0]
            write_png(preview_dir / f"{actor_names[species_index]}_{anim_names[animation_index]}.png",
                      frame["width"], frame["height"], frame["pixels"], preview_palette)
    for index, floor in enumerate(floors[:32]):
        write_png(preview_dir / f"cave_floor_{index:02d}.png", 40, 20, floor, preview_palette)
    for index, rock in enumerate(rocks):
        write_png(preview_dir / f"cave_rock_{index:02d}.png", rock["width"], rock["height"],
                  rock["pixels"], preview_palette)
    manifest = {
        "schema_version": 1,
        "importer_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "header_sha256": hashlib.sha256(header.read_bytes()).hexdigest(),
        "source_installer": str(installer),
        "source_sha256": hashlib.sha256(installer.read_bytes()).hexdigest(),
        "stormlib": "v9.30 @ 86f9b99ffe4d3417dad16d00541cf6f2e3d7bf79",
        "reader_commit": REF_COMMIT,
        "palette_source": r"data\global\palette\ACT1\pal.dat",
        "tile_source": r"data\global\tiles\ACT1\Caves\cave.dt1",
        "floor_tiles": len(floors),
        "rock_tiles": [{key: rock[key] for key in
                        ("source_tile", "type", "style", "sequence", "width", "height", "x", "y")}
                       for rock in rocks],
        "actor_animation_frames": [[animation["frames"] for animation in species] for species in actors],
        "directions": 8,
        "scale": 4,
        "direction_mapping_root_to_d2": [6, 2, 7, 3, 4, 0, 5, 1],
    }
    (output_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def rgb555_palette(data: bytes) -> list[int]:
    """Read D2's 768-byte BGR palette and map entries to GBA RGB555."""
    if len(data) < 768:
        raise ValueError("palette must contain 256 BGR triples (768 bytes)")
    palette = []
    for i in range(256):
        b, g, r = data[i*3:i*3+3]
        # GBA BGR555 stores red in the low five bits and blue in the high five.
        palette.append((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10))
    return palette


def reduce_palette(palette: list[int], images: Iterable[bytes], size: int = 224) -> tuple[list[int], list[int]]:
    """Build a shared GBA palette and an old-index to new-index map."""
    if len(palette) != 256 or not 2 <= size <= 256:
        raise ValueError("palette reduction needs 256 colours and a target size of 2..256")
    from collections import Counter
    counts = Counter(pixel for image in images for pixel in image if pixel)
    keep = [0] + [index for index, _ in sorted(counts.items(), key=lambda pair: (-pair[1], pair[0]))[:size - 1]]
    if len(keep) < size:
        keep.extend(index for index in range(1, 256) if index not in keep)
        keep = keep[:size]
    kept_colors = [palette[index] for index in keep]
    mapping = [0] * 256
    mapping[0] = 0
    for index in range(1, 256):
        color = palette[index]
        r, g, b = (color >> 10) & 31, (color >> 5) & 31, color & 31
        best = min(range(1, len(kept_colors)), key=lambda candidate:
                   _rgb555_distance(color, kept_colors[candidate]))
        mapping[index] = best
    return kept_colors, mapping


def _rgb555_distance(a: int, b: int) -> int:
    return (((a >> 10) & 31) - ((b >> 10) & 31)) ** 2 + (((a >> 5) & 31) - ((b >> 5) & 31)) ** 2 + ((a & 31) - (b & 31)) ** 2


def emit_header(path: Path, actors: list[list[dict]], floors: list[bytes],
                palette: list[int], *, directions: int = 8,
                rocks: list[dict] | None = None) -> None:
    """Emit root renderer's compact actor/floor/palette C API.

    `actors` has four species, each with five animations (neutral/walk/
    attack/hit/death), each animation containing `dirs*frames` decoded frames.
    Frame pixels are already reduced to palette indices 0..223 and scaled.
    """
    if len(actors) != 4 or any(len(species) != 5 for species in actors):
        raise ValueError("expected four actor species with five animations each")
    if len(palette) != 224 or any(len(floor) != 800 for floor in floors):
        raise ValueError("expected a 224-colour palette and 40x20 floor buffers")
    lines = ["/* Generated privately from user-supplied Diablo II data by tools/d2_import.py.",
             " * Source: pinned references/d2decomped @ " + REF_COMMIT,
             " * Derived copyrighted data: keep under .local/d2/generated/. */",
             "#ifndef D2_DEN_ASSETS_H", "#define D2_DEN_ASSETS_H",
             '#include "gba.h"',
             "typedef struct { u16 w, h; s16 x, y; const u8 *pixels; } D2SpriteFrame;",
             "typedef struct { u16 frames; u8 dirs; const D2SpriteFrame *images; } D2Animation;", ""]
    names = ("barbarian", "zombie", "fallen", "shaman")
    anim_names = ("neutral", "walk", "attack", "hit", "death")
    for species_index, species in enumerate(actors):
        for anim_index, animation in enumerate(species):
            frames = animation["images"]
            per_dir = animation["frames"]
            if len(frames) != directions * per_dir:
                raise ValueError(f"{names[species_index]}/{anim_names[anim_index]}: frame count mismatch")
            frame_symbol = f"d2_{names[species_index]}_{anim_names[anim_index]}_frames"
            pixel_symbols = []
            for i, frame in enumerate(frames):
                pixels = frame["pixels"]
                w, h = frame["width"], frame["height"]
                if len(pixels) != w * h or w > 65535 or h > 65535:
                    raise ValueError("invalid or oversized sprite frame")
                pixel_symbol = f"{frame_symbol}_pixels_{i}"
                pixel_symbols.append(pixel_symbol)
                lines.append(f"static const u8 {pixel_symbol}[{len(pixels)}] = {{")
                lines.extend("    " + ", ".join(str(value) for value in pixels[pos:pos+24]) + ","
                             for pos in range(0, len(pixels), 24))
                lines.append("};")
            lines.append(f"static const D2SpriteFrame {frame_symbol}[{len(frames)}] = {{")
            for i, frame in enumerate(frames):
                w, h = frame["width"], frame["height"]
                pixel_symbol = pixel_symbols[i]
                lines.append(f"    {{{w}u, {h}u, {frame['x']} , {frame['y']} , {pixel_symbol}}},")
            lines.extend(["};", ""])
    lines.append("static const D2Animation d2_den_actors[4][5] = {")
    for species_index, species in enumerate(actors):
        entries = []
        for anim_index, animation in enumerate(species):
            symbol = f"d2_{names[species_index]}_{anim_names[anim_index]}_frames"
            entries.append(f"{{{animation['frames']}u, {directions}u, {symbol}}}")
        lines.append("    {" + ", ".join(entries) + "},")
    lines.extend(["};", f"static const u16 d2_den_floor_count = {len(floors)}u;",
                  f"static const u8 d2_den_floors[{max(1, len(floors))}][800] = {{"])
    for floor in floors:
        lines.append("    {")
        lines.extend("        " + ", ".join(str(value) for value in floor[pos:pos+32]) + ","
                     for pos in range(0, 800, 32))
        lines.append("    },")
    rocks = rocks or []
    lines.extend(["};", f"static const u16 d2_den_rock_count = {len(rocks)}u;"])
    if rocks:
        rock_frames = []
        for i, rock in enumerate(rocks):
            symbol = f"d2_den_rock_pixels_{i}"
            rock_frames.append(symbol)
            pixels = rock["pixels"]
            lines.append(f"static const u8 {symbol}[{len(pixels)}] = {{")
            lines.extend("    " + ", ".join(str(value) for value in pixels[pos:pos+24]) + ","
                         for pos in range(0, len(pixels), 24))
            lines.append("};")
        lines.append(f"static const D2SpriteFrame d2_den_rocks[{len(rocks)}] = {{")
        for rock, symbol in zip(rocks, rock_frames):
            lines.append(f"    {{{rock['width']}u, {rock['height']}u, {rock['x']}, {rock['y']}, {symbol}}},")
        lines.append("};")
    lines.append("static const u16 d2_den_palette[224] = {")
    lines.extend("    " + ", ".join(f"0x{color:04X}u" for color in palette[pos:pos+16]) + ","
                 for pos in range(0, 224, 16))
    lines.extend(["};", "#endif /* D2_DEN_ASSETS_H */", ""])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="ascii")


def write_png(path: Path, width: int, height: int, indices: bytes,
              palette: Iterable[tuple[int, int, int, int]]) -> None:
    """Write an indexed sprite PNG without third-party Python packages."""
    colors = list(palette)
    if len(colors) != 256 or len(indices) != width * height:
        raise ValueError("PNG requires a 256-entry palette and width*height indices")
    def chunk(tag: bytes, payload: bytes) -> bytes:
        return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", zlib.crc32(tag + payload) & 0xffffffff)
    raw = b"".join(b"\0" + indices[y*width:(y+1)*width] for y in range(height))
    plte = bytes(component for color in colors for component in color[:3])
    trns = bytes(color[3] for color in colors)
    blob = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">2I5B", width, height, 8, 3, 0, 0, 0))
            + chunk(b"PLTE", plte) + chunk(b"tRNS", trns)
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(blob)


def stormlib_path() -> str | None:
    """Locate system StormLib or the pinned local reference build."""
    from ctypes.util import find_library

    # Prefer the explicitly pinned local build so preparation and import use
    # the same StormLib version on macOS, Linux, and Windows.
    build = ROOT / ".local/d2/build/stormlib"
    local_candidates = (
        build / "storm.framework/storm",
        build / "libstorm.so",
        build / "libstorm.dylib",
        build / "storm.dll",
        build / "libStormLib.so",
        build / "libStormLib.dylib",
        build / "StormLib.dll",
    )
    for candidate in local_candidates:
        if candidate.is_file():
            return str(candidate)
    return find_library("storm") or find_library("StormLib")


def check_stormlib() -> str | None:
    return stormlib_path()


def resolve_mpq(path: Path) -> Path:
    """Accept an MPQ or unpack the valid MPQ appended to Blizzard's installer EXE."""
    cache = ROOT / ".local/d2/cache"
    extracted = cache / (path.stem + "_embedded.mpq")
    if extracted.exists() and extracted.stat().st_mtime >= path.stat().st_mtime:
        return extracted
    data = path.read_bytes()
    if data.startswith(b"MPQ\x1a"):
        return path
    candidates = []
    cursor = 0
    while True:
        offset = data.find(b"MPQ\x1a", cursor)
        if offset < 0:
            break
        cursor = offset + 1
        if offset + 32 > len(data):
            continue
        header_size, archive_size = struct.unpack_from("<II", data, offset + 4)
        version = struct.unpack_from("<H", data, offset + 12)[0]
        if 32 <= header_size <= 4096 and version <= 4 and archive_size >= header_size and offset + archive_size <= len(data):
            candidates.append((offset, archive_size))
    if not candidates:
        raise ValueError(f"no complete embedded MPQ archive found in {path}")
    # Blizzard installer media appends its data archive; requiring an exact end
    # match avoids selecting executable code that merely contains the magic.
    offset, size = next((candidate for candidate in reversed(candidates)
                         if candidate[0] + candidate[1] == len(data)), candidates[-1])
    cache.mkdir(parents=True, exist_ok=True)
    if not extracted.exists() or extracted.stat().st_size != size:
        extracted.write_bytes(data[offset:offset + size])
    return extracted


def extract_mpq_member(archive_path: Path, member: str) -> bytes:
    """Read and decompress one named member via StormLib's public C API."""
    library = stormlib_path()
    if not library:
        raise RuntimeError("StormLib is unavailable; build the pinned copy with the documented CMake command")
    storm = ctypes.CDLL(library)
    handle_t = ctypes.c_void_p
    storm.SFileOpenArchive.argtypes = [ctypes.c_char_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(handle_t)]
    storm.SFileOpenArchive.restype = ctypes.c_int
    storm.SFileOpenFileEx.argtypes = [handle_t, ctypes.c_char_p, ctypes.c_uint32, ctypes.POINTER(handle_t)]
    storm.SFileOpenFileEx.restype = ctypes.c_int
    storm.SFileGetFileSize.argtypes = [handle_t, ctypes.POINTER(ctypes.c_uint32)]
    storm.SFileGetFileSize.restype = ctypes.c_uint32
    storm.SFileReadFile.argtypes = [handle_t, ctypes.c_void_p, ctypes.c_uint32,
                                    ctypes.POINTER(ctypes.c_uint32), ctypes.c_void_p]
    storm.SFileReadFile.restype = ctypes.c_int
    storm.SFileCloseFile.argtypes = [handle_t]
    storm.SFileCloseFile.restype = ctypes.c_int
    storm.SFileCloseArchive.argtypes = [handle_t]
    storm.SFileCloseArchive.restype = ctypes.c_int
    archive_path = resolve_mpq(archive_path)
    archive = handle_t()
    if not storm.SFileOpenArchive(str(archive_path).encode(), 0, 0x100, ctypes.byref(archive)):
        raise RuntimeError(f"StormLib could not open MPQ archive: {archive_path}")
    file_handle = handle_t()
    try:
        if not storm.SFileOpenFileEx(archive, member.replace("/", "\\").encode(), 0, ctypes.byref(file_handle)):
            raise FileNotFoundError(f"MPQ member not found: {member}")
        high = ctypes.c_uint32()
        low = storm.SFileGetFileSize(file_handle, ctypes.byref(high))
        size = (high.value << 32) | low
        if size > 128 * 1024 * 1024:
            raise ValueError(f"refusing oversized MPQ member ({size} bytes): {member}")
        out = bytearray()
        while len(out) < size:
            chunk_size = min(1 << 20, size - len(out))
            buffer = ctypes.create_string_buffer(chunk_size)
            got = ctypes.c_uint32()
            if not storm.SFileReadFile(file_handle, buffer, chunk_size, ctypes.byref(got), None):
                raise IOError(f"StormLib failed reading MPQ member {member} at {len(out)}")
            if got.value == 0:
                raise IOError(f"unexpected end of MPQ member {member}")
            out.extend(buffer.raw[:got.value])
        return bytes(out)
    finally:
        if file_handle:
            storm.SFileCloseFile(file_handle)
        storm.SFileCloseArchive(archive)


def list_mpq_members(archive_path: Path) -> list[str]:
    """Enumerate names from an MPQ listfile using StormLib."""
    library = stormlib_path()
    if not library:
        raise RuntimeError("StormLib is unavailable")
    class FindData(ctypes.Structure):
        _fields_ = [("name", ctypes.c_char * 1024), ("plain_name", ctypes.c_void_p),
                    ("hash_index", ctypes.c_uint32), ("block_index", ctypes.c_uint32),
                    ("file_size", ctypes.c_uint32), ("file_flags", ctypes.c_uint32),
                    ("comp_size", ctypes.c_uint32), ("time_lo", ctypes.c_uint32),
                    ("time_hi", ctypes.c_uint32), ("locale", ctypes.c_uint32)]
    storm = ctypes.CDLL(library)
    handle_t = ctypes.c_void_p
    storm.SFileOpenArchive.argtypes = [ctypes.c_char_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(handle_t)]
    storm.SFileOpenArchive.restype = ctypes.c_int
    storm.SFileFindFirstFile.argtypes = [handle_t, ctypes.c_char_p, ctypes.POINTER(FindData), ctypes.c_char_p]
    storm.SFileFindFirstFile.restype = handle_t
    storm.SFileFindNextFile.argtypes = [handle_t, ctypes.POINTER(FindData)]
    storm.SFileFindNextFile.restype = ctypes.c_int
    storm.SFileFindClose.argtypes = [handle_t]
    storm.SFileFindClose.restype = ctypes.c_int
    storm.SFileCloseArchive.argtypes = [handle_t]
    storm.SFileCloseArchive.restype = ctypes.c_int
    archive_path = resolve_mpq(archive_path)
    archive = handle_t()
    if not storm.SFileOpenArchive(str(archive_path).encode(), 0, 0x100, ctypes.byref(archive)):
        raise RuntimeError(f"StormLib could not open MPQ archive: {archive_path}")
    find_handle = handle_t()
    result = []
    try:
        data = FindData()
        find_handle = storm.SFileFindFirstFile(archive, b"*", ctypes.byref(data), None)
        if not find_handle:
            return result
        while True:
            name = bytes(data.name).split(b"\0", 1)[0]
            if name:
                result.append(name.decode("cp1252", "replace"))
            if not storm.SFileFindNextFile(find_handle, ctypes.byref(data)):
                break
        return sorted(set(result), key=str.casefold)
    finally:
        if find_handle:
            storm.SFileFindClose(find_handle)
        storm.SFileCloseArchive(archive)


def self_check() -> dict:
    # One tiny synthetic DC6 with a single opaque pixel and one scanline.
    rle = bytes((1, 7, 0x80))
    frame_off = 28
    frame = struct.pack("<8I", 0, 1, 1, 0, 0, 0, 0, len(rle)) + rle + b"\xee\xee\xee"
    raw = struct.pack("<6I", 6, 0, 0, 0, 1, 1) + struct.pack("<I", frame_off) + frame
    dirs, count, frames = dc6_frames(raw)
    assert (dirs, count, frames[0]["pixels"]) == (1, 1, b"\x07")
    return {"dc6": "ok", "dc6_frames": len(frames), "reference_commit": REF_COMMIT,
            "stormlib": check_stormlib()}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-check", action="store_true", help="validate the Python DC6 reader")
    parser.add_argument("--source-dir", type=Path, default=DEFAULT_SOURCE,
                        help="directory of assets extracted from a user-supplied MPQ")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--den-demo", type=Path,
                        help="import original sprites, cave floors, palette, and tables from the official demo installer")
    parser.add_argument("--mpq", type=Path, help="user-supplied MPQ archive")
    parser.add_argument("--list", action="store_true", help="list member names from --mpq")
    parser.add_argument("--extract", nargs="+", metavar="MEMBER",
                        help="extract named MPQ member(s) into --source-dir")
    parser.add_argument("--inspect", type=Path, help="inspect a loose .dc6, .dcc, .cof, or .dt1 file")
    args = parser.parse_args(argv)
    if args.self_check:
        print(json.dumps(self_check(), indent=2))
        return 0
    if args.den_demo:
        result = import_den_demo(args.den_demo, args.output_dir)
        print(json.dumps(result, indent=2))
        return 0
    if args.mpq and args.list and not args.extract:
        print("\n".join(list_mpq_members(args.mpq)))
        return 0
    if args.mpq and args.extract:
        for member in args.extract:
            # Restrict outputs to a relative, archive-style path below source-dir.
            relative = Path(member.replace("\\", "/"))
            if relative.is_absolute() or ".." in relative.parts:
                parser.error(f"unsafe MPQ member output path: {member}")
            output = args.source_dir / relative
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(extract_mpq_member(args.mpq, member))
            print(f"extracted {member} -> {output.relative_to(ROOT)}")
        return 0
    if args.inspect:
        raw = args.inspect.read_bytes()
        suffix = args.inspect.suffix.lower()
        if suffix == ".dc6":
            dirs, per, frames = dc6_frames(raw)
            result = {"format": "DC6", "directions": dirs, "frames_per_direction": per,
                      "frames": [{k: v for k, v in frame.items() if k != "pixels"} for frame in frames]}
        elif suffix == ".dcc":
            dirs, per, frames = dcc_frames(raw)
            result = {"format": "DCC", "directions": dirs, "frames_per_direction": per,
                      "frames": [{k: v for k, v in frame.items() if k != "pixels"} for frame in frames]}
        elif suffix == ".dt1":
            tiles = dt1_tiles(raw)
            result = {"format": "DT1", "tiles": len(tiles),
                      "floor_candidates": sum(tile["type"] == 0 and tile["width"] and tile["height"] for tile in tiles),
                      "tile_headers": [{k: v for k, v in tile.items() if k != "pixels"} for tile in tiles]}
        else:
            result = cof_info(raw) if suffix == ".cof" else None
        if result is None:
            parser.error("--inspect accepts .dc6, .dcc, .cof, or .dt1")
        print(json.dumps(result, indent=2))
        return 0
    if args.mpq or args.extract or args.list:
        parser.error("use --mpq with --list or with --extract MEMBER...")
    if not args.source_dir.exists():
        parser.error(f"no extracted source directory at {args.source_dir}; point --source-dir at files extracted from your own MPQ")
    if check_stormlib() is None:
        parser.error("StormLib is required to read MPQ archives; this machine has no StormLib library. Provide --source-dir with extracted files or install StormLib.")
    parser.error("select --mpq and --extract MEMBER... to retrieve assets, or use --inspect on loose DC6/DCC/COF files")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
