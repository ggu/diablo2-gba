#!/usr/bin/env python3
"""Fetch and privately unpack Blizzard's freely distributed Diablo II demo.

The downloaded installer and extracted game data stay under the ignored
``.local/d2`` directory. The installer is treated as data and is never run.

Copyright (C) 2026 blizzard-gba contributors
SPDX-License-Identifier: GPL-3.0-or-later
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import sys
import tempfile
from pathlib import Path
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
DEMO_URL = "https://archive.org/download/DiabloIiDemo/DiabloIIDemo.exe"
DEMO_SIZE = 138_309_685
DEMO_MD5 = "9ae5033551a078937cd5d1f388cd8438"
DEMO_SHA256 = "89352716523e474514553e2092a1ae9349c5c7ff9e79c7861dd65fe19be88b61"
DEMO_PATH = ROOT / ".local/d2/downloads/diabloiidemo.exe"
DOWNLOAD_MANIFEST = ROOT / ".local/d2/download-manifest.json"
SOURCE_DIR = ROOT / ".local/d2/source"
MPQ_OFFSET = 0x4F000


def _download_verified(response, destination: Path, *, size: int, md5: str,
                        sha256: str) -> None:
    """Stream a response to a sibling temporary file and atomically install it."""
    destination.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary_name = tempfile.mkstemp(prefix=f".{destination.name}.",
                                          suffix=".partial", dir=destination.parent)
    temporary = Path(temporary_name)
    md5_hash, sha_hash = hashlib.md5(), hashlib.sha256()
    total = 0
    try:
        with os.fdopen(fd, "wb") as output:
            while True:
                chunk = response.read(1024 * 1024)
                if not chunk:
                    break
                total += len(chunk)
                if total > size:
                    raise ValueError(f"download exceeds expected size ({size} bytes)")
                md5_hash.update(chunk)
                sha_hash.update(chunk)
                output.write(chunk)
            output.flush()
            os.fsync(output.fileno())
        if total != size:
            raise ValueError(f"download size mismatch: expected {size}, got {total}")
        if md5_hash.hexdigest() != md5.lower():
            raise ValueError(f"download MD5 mismatch: {md5_hash.hexdigest()}")
        if sha_hash.hexdigest() != sha256.lower():
            raise ValueError(f"download SHA-256 mismatch: {sha_hash.hexdigest()}")
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)


def download_demo(destination: Path = DEMO_PATH) -> Path:
    """Fetch only the freely distributed demo, verifying its published hashes."""
    if destination.is_file():
        size, md5, sha256 = hash_file(destination)
        if (size, md5, sha256) == (DEMO_SIZE, DEMO_MD5, DEMO_SHA256):
            record_download(destination)
            return destination
    with urlopen(DEMO_URL, timeout=60) as response:
        _download_verified(response, destination, size=DEMO_SIZE,
                           md5=DEMO_MD5, sha256=DEMO_SHA256)
    record_download(destination)
    return destination


def hash_file(path: Path) -> tuple[int, str, str]:
    md5_hash, sha_hash = hashlib.md5(), hashlib.sha256()
    size = 0
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            size += len(chunk)
            md5_hash.update(chunk)
            sha_hash.update(chunk)
    return size, md5_hash.hexdigest(), sha_hash.hexdigest()


def record_download(path: Path, manifest_path: Path = DOWNLOAD_MANIFEST) -> None:
    """Record a verified demo while preserving unrelated provenance entries."""
    size, md5, sha256 = hash_file(path)
    if (size, md5, sha256) != (DEMO_SIZE, DEMO_MD5, DEMO_SHA256):
        raise ValueError("refusing to record an unverified demo")
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if not isinstance(manifest, dict):
            manifest = {}
    except (OSError, json.JSONDecodeError):
        manifest = {}
    assets = manifest.get("assets")
    if not isinstance(assets, list):
        assets = []
    record = {
        "filename": path.name,
        "url": DEMO_URL,
        "size_bytes": size,
        "md5": md5,
        "sha256": sha256,
        "provenance": "Freely distributed Diablo II demo; verified against published size and MD5, plus pinned SHA-256.",
        "cross_checks": [
            "Archive.org metadata reports the published 138,309,685-byte demo and matching MD5.",
            "Installer contains a bounded MPQ archive at offset 0x4f000 ending at EOF.",
        ],
    }
    assets = [item for item in assets if not (
        isinstance(item, dict) and item.get("filename") == path.name)]
    assets.append(record)
    manifest["assets"] = assets
    fd, temporary_name = tempfile.mkstemp(prefix=f".{manifest_path.name}.", suffix=".partial",
                                          dir=manifest_path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as output:
            json.dump(manifest, output, indent=2, sort_keys=True)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, manifest_path)
    finally:
        temporary.unlink(missing_ok=True)


def validate_embedded_mpq(installer: Path) -> tuple[int, int]:
    """Check the known embedded MPQ header and EOF bounds without extracting it.

    The actual archive slicing is delegated to ``d2_import.resolve_mpq`` so
    there is one implementation of the MPQ extraction logic.
    """
    file_size = installer.stat().st_size
    with installer.open("rb") as source:
        source.seek(MPQ_OFFSET)
        header = source.read(32)
    if len(header) < 32 or header[:4] != b"MPQ\x1a":
        raise ValueError(f"no MPQ header at expected installer offset 0x{MPQ_OFFSET:x}")
    header_size, archive_size = struct.unpack_from("<II", header, 4)
    format_version = struct.unpack_from("<H", header, 12)[0]
    if not (32 <= header_size <= 4096 and header_size <= archive_size
            and format_version <= 4):
        raise ValueError("invalid embedded MPQ header fields")
    if MPQ_OFFSET + archive_size != file_size:
        raise ValueError("embedded MPQ bounds do not end at installer EOF")
    return MPQ_OFFSET, archive_size


def extract_first_level(installer: Path, output_dir: Path = SOURCE_DIR) -> int:
    """List MPQ entries with StormLib and extract only top-level members."""
    try:
        from tools.d2_import import extract_mpq_member, list_mpq_members, resolve_mpq
    except ModuleNotFoundError:  # direct ``python tools/d2_fetch_demo.py`` entry point
        from d2_import import extract_mpq_member, list_mpq_members, resolve_mpq

    validate_embedded_mpq(installer)
    archive = resolve_mpq(installer)
    members = list_mpq_members(archive)
    output_dir.mkdir(parents=True, exist_ok=True)
    extracted = 0
    for member in members:
        # The demo listfile uses root-level FileNNNNNNNN.ext entries. Avoid
        # paths, traversal, and nested MPQs; another tool can inspect those.
        if (not member or "/" in member or "\\" in member or ":" in member
                or member in {".", ".."} or Path(member).name != member):
            continue
        target = output_dir / member
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(extract_mpq_member(archive, member))
        extracted += 1
    return extracted


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--download-only", action="store_true",
                        help="download and verify the demo without extracting MPQ files")
    parser.add_argument("--installer", type=Path, default=DEMO_PATH,
                        help="verified local demo installer (default: ignored .local path)")
    parser.add_argument("--source-dir", type=Path, default=SOURCE_DIR,
                        help="private output directory for first-level MPQ members")
    args = parser.parse_args(argv)
    try:
        if args.installer == DEMO_PATH:
            installer = download_demo(args.installer)
        else:
            installer = args.installer
        size, md5, sha256 = hash_file(installer)
        if (size, md5, sha256) != (DEMO_SIZE, DEMO_MD5, DEMO_SHA256):
            raise ValueError("installer does not match the pinned free demo archive")
        if args.download_only:
            print(f"verified {installer} ({size} bytes)")
            return 0
        count = extract_first_level(installer, args.source_dir)
        print(f"verified {installer}; extracted {count} top-level members to {args.source_dir}")
        return 0
    except (OSError, ValueError, RuntimeError) as error:
        print(f"d2_fetch_demo: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
