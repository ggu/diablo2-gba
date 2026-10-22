#!/usr/bin/env python3
"""Generate the small item-definition table used by the Den of Evil slice.

Reads user-supplied 1.04 demo Excel tables from .local/d2/extracted and writes
only this deliberately limited, normal-quality subset to .local/d2/generated.
No game data is distributed with the repository.
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / ".local/d2/extracted/data/GLOBAL/Excel"
OUT = ROOT / ".local/d2/generated/d2_den_items.h"

# Stable IDs consumed by the game and renderer. Table names/codes are verified
# against the installed demo tables at generation time.
ITEMS = [
    (1, "DEN_ITEM_HAND_AXE", "weapons", "Hand Axe", "hax", "weapon", 0),
    (2, "DEN_ITEM_BUCKLER", "armor", "Buckler", "buc", "shield", 1),
    (3, "DEN_ITEM_CAP", "armor", "Cap/hat", "cap", "armor", 2),
    (4, "DEN_ITEM_QUILTED_ARMOR", "armor", "Quilted Armor", "qui", "armor", 3),
    (5, "DEN_ITEM_LEATHER_BOOTS", "armor", "Leather Boots", "lbt", "armor", 6),
    (6, "DEN_ITEM_LEATHER_GLOVES", "armor", "Gloves(L)", "lgl", "armor", 4),
    (7, "DEN_ITEM_SASH", "armor", "Sash(L)", "lbl", "armor", 5),
    (8, "DEN_ITEM_CLUB", "weapons", "Club", "clb", "weapon", 0),
    (9, "DEN_ITEM_SCIMITAR", "weapons", "Scimitar", "scm", "weapon", 0),
]


def read_rows(table: str) -> dict[str, dict[str, str]]:
    path = SRC / f"{table}.txt"
    with path.open(encoding="latin1", newline="") as stream:
        return {row["code"]: row for row in csv.DictReader(stream, delimiter="\t")}


def number(row: dict[str, str], col: str, default: int = 0) -> int:
    value = row.get(col, "")
    return default if not value else int(float(value))


def generate() -> str:
    tables = {name: read_rows(name) for name in ("weapons", "armor")}
    with (SRC / "charstats.txt").open(encoding="latin1", newline="") as stream:
        charstats = next(row for row in csv.DictReader(stream, delimiter="\t")
                         if row["class"] == "Barbarian")
    block_factor = number(charstats, "BlockFactor")
    to_hit_factor = number(charstats, "ToHitFactor")
    rows = []
    for ident, symbol, table, name, code, category, slot in ITEMS:
        row = tables[table].get(code)
        if not row or row["name"] != name:
            raise ValueError(f"expected {name} ({code}) in {table}.txt")
        # Preserve the user's demo-table values; exclude non-base/unspawnable
        # records and keep the starter loot ordinary, without magical affixes.
        if number(row, "spawnable") != 1:
            raise ValueError(f"{name} is not marked spawnable in demo data")
        if table == "weapons":
            dmg = (number(row, "mindam"), number(row, "maxdam"))
            defense = (0, 0)
        else:
            dmg = (number(row, "mindam"), number(row, "maxdam"))
            defense = (number(row, "minac"), number(row, "maxac"))
        rows.append((ident, symbol, name, code, category, number(row, "invwidth"),
                     number(row, "invheight"), dmg[0], dmg[1], defense[0], defense[1],
                     number(row, "reqstr"), number(row, "reqdex"), 0,
                     number(row, "durability"), number(row, "block"), slot))

    out = ["/* Generated from locally supplied demo Excel tables; do not commit. */",
           "enum { DEN_ITEM_NONE = 0, " + ", ".join(f"{r[1]} = {r[0]}" for r in rows) + " };",
           f"#define DEN_BARB_BLOCK_FACTOR {block_factor}",
           f"#define DEN_BARB_TO_HIT_FACTOR {to_hit_factor}",
           "static const DenItemDef den_item_defs[] = {"]
    for r in rows:
        ident, symbol, name, code, category, w, h, md, xd, mn, mx, rs, rd, lvl, dura, block, slot = r
        cat = {"weapon": "DEN_ITEM_WEAPON", "shield": "DEN_ITEM_SHIELD", "armor": "DEN_ITEM_ARMOR"}[category]
        escaped = name.replace("\\", "\\\\").replace('"', '\\"')
        out.append(f'  {{{ident}, "{escaped}", "{code}", {cat}, {w}, {h}, {md}, {xd}, {mn}, {mx}, {rs}, {rd}, {lvl}, {dura}, {block}, {ident}, {slot}}},')
    out += ["};", f"#define DEN_ITEM_DEF_COUNT {len(rows)}", ""]
    return "\n".join(out)


def main() -> None:
    global SRC
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=SRC, help="Excel table directory")
    parser.add_argument("--output", type=Path, default=OUT, help="generated header path")
    args = parser.parse_args()
    SRC = args.source
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generate(), encoding="ascii")


if __name__ == "__main__":
    main()
