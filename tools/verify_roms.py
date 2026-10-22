#!/usr/bin/env python3
"""Check GBA boot headers, identifiers, ROM size and linked program size."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
TARGETS = {
    "--den": ("Diablo2-Den-of-Evil-GBA", "diablo2-den", "D2DE"),
}


def executable(name: str) -> str:
    prefix = os.environ.get("PREFIX", "arm-none-eabi-")
    command = shutil.which(prefix + name)
    if command is None:
        raise SystemExit(f"Required tool not found: {prefix}{name}")
    return command


def verify(rom_name: str, elf_name: str, code: str) -> dict[str, object]:
    rom = ROOT / "build" / f"{rom_name}.gba"
    data = rom.read_bytes()
    if not 192 <= len(data) <= 32 * 1024 * 1024 or len(data) & (len(data) - 1):
        raise ValueError(f"{rom.name}: invalid ROM size {len(data)}")
    if data[0xB2] != 0x96:
        raise ValueError(f"{rom.name}: invalid Nintendo logo checksum byte")
    if data[0xBD] != (-sum(data[0xA0:0xBD]) - 0x19) & 0xFF:
        raise ValueError(f"{rom.name}: invalid header checksum")
    if data[0xAC:0xB0].decode("ascii", errors="replace") != code:
        raise ValueError(f"{rom.name}: expected game code {code}")
    if b"SRAM_V113" not in data:
        raise ValueError(f"{rom.name}: missing SRAM detection signature")
    size_output = subprocess.check_output(
        [executable("size"), str(ROOT / "build" / f"{elf_name}.elf")], text=True
    )
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(), "size": size_output.strip()}


def main(argv: list[str]) -> int:
    selected = [flag for flag in TARGETS if flag in argv]
    if len(selected) > 1:
        raise SystemExit("Select only one optional ROM profile per verification run")
    flag = selected[0] if selected else None
    rom_name, elf_name, code = TARGETS[flag] if flag else ("Diablo2-GBA", "diablo2", "D2GD")
    result = verify(rom_name, elf_name, code)
    print(f"{rom_name}: {result['bytes']} bytes, SHA-256 {result['sha256']}")
    output = ROOT / "build" / ("den-verification.json" if flag else "verification.json")
    output.write_text(json.dumps({rom_name: result}, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))
