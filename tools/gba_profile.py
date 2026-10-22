#!/usr/bin/env python3
"""Generic GBA hardware profiling helpers shared by standalone game tools.

Copyright (C) 2026 blizzard-gba contributors
SPDX-License-Identifier: GPL-3.0-or-later
"""
from __future__ import annotations

try:  # package import for tests; direct import for script entry points
    from .emulator_smoke import RSP
except ImportError:
    from emulator_smoke import RSP


def hardware_clock(rsp: RSP) -> int:
    """Read the 32-bit GBA timer value assembled from cascaded timers 2/3."""
    data = rsp.read(0x04000108, 8)
    return int.from_bytes(data[:2], "little") | (int.from_bytes(data[4:6], "little") << 16)


def read_u32(rsp: RSP, address: int) -> int:
    return int.from_bytes(rsp.read(address, 4), "little")
