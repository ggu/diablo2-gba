#!/usr/bin/env python3
"""Prepare and report the private Diablo II Den of Evil asset workspace."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))
from d2_fetch_demo import DEMO_PATH, DEMO_SHA256, SOURCE_DIR  # noqa: E402
import d2_import  # noqa: E402

D2DECOMP_URL = "https://github.com/psi29a/d2decomped.git"
D2DECOMP_COMMIT = "f344332ce4b2e42e54ca3b5d7c8e480f49d823ee"
STORMLIB_URL = "https://github.com/ladislav-zezula/StormLib.git"
STORMLIB_COMMIT = "86f9b99ffe4d3417dad16d00541cf6f2e3d7bf79"
D2DECOMP_PATH = ROOT / "references/d2decomped"
STORMLIB_PATH = ROOT / ".local/d2/references/StormLib"
STORMLIB_BUILD = ROOT / ".local/d2/build/stormlib"
STORMLIB_LIBRARY = STORMLIB_BUILD / "storm.framework/storm"
ASSET_DIR = ROOT / ".local/d2/generated"
ASSET_HEADER = ASSET_DIR / "d2_den_assets.h"
ASSET_MANIFEST = ASSET_DIR / "manifest.json"
ASSET_SCHEMA_VERSION = 1
IMPORTER_PATH = TOOLS / "d2_import.py"
DOWNLOAD_MANIFEST = ROOT / ".local/d2/download-manifest.json"
ROM_PATH = ROOT / "build/Diablo2-Den-of-Evil-GBA.gba"
ITEM_HEADER = ASSET_DIR / "d2_den_items.h"
TOWN_HEADER = ASSET_DIR / "d2_town_assets.h"
EXTRA_MANIFEST = ASSET_DIR / "den-extra-manifest.json"
EXTRA_FILES = (ITEM_HEADER, TOWN_HEADER, TOOLS / "d2_items_pack.py", TOOLS / "d2_town_assets.py")
ITEM_TABLE_DIR = ROOT / ".local/d2/extracted/data/GLOBAL/Excel"
ITEM_TABLE_MEMBERS = ("data\\global\\excel\\weapons.txt",
                      "data\\global\\excel\\armor.txt",
                      "data\\global\\excel\\charstats.txt")


class WorkspaceError(RuntimeError):
    pass


def _run(command: list[str], *, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    try:
        result = subprocess.run(command, cwd=cwd, check=True, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except FileNotFoundError as error:
        raise WorkspaceError(f"required command not found: {command[0]}") from error
    except subprocess.CalledProcessError as error:
        detail = error.stdout.strip()
        raise WorkspaceError(f"command failed ({error.returncode}): {' '.join(command)}"
                             + (f"\n{detail}" if detail else "")) from error
    if result.stdout.strip():
        print(result.stdout.rstrip())
    return result


def _git_head(path: Path) -> str | None:
    if not path.exists():
        return None
    if not (path / ".git").exists():
        raise WorkspaceError(f"expected a Git checkout at {path}; refusing to replace it")
    try:
        return subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
    except (FileNotFoundError, subprocess.CalledProcessError) as error:
        raise WorkspaceError(f"cannot read Git HEAD at {path}") from error


def ensure_checkout(path: Path, url: str, commit: str) -> dict:
    """Clone an absent dependency; accept an existing checkout only at its pin."""
    head = _git_head(path)
    if head is not None:
        if head != commit:
            raise WorkspaceError(f"{path} is at {head}, expected pinned commit {commit}; "
                                 "no checkout changes were made")
        dirty = bool(subprocess.check_output(["git", "-C", str(path), "status", "--porcelain"], text=True).strip())
        return {"path": str(path), "commit": head, "pinned": True, "dirty": dirty, "cloned": False}

    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f".{path.name}.clone-", dir=path.parent))
    temporary.rmdir()
    try:
        _run(["git", "clone", "--no-checkout", url, str(temporary)])
        _run(["git", "-C", str(temporary), "fetch", "--no-tags", "origin", commit])
        _run(["git", "-C", str(temporary), "checkout", "--detach", commit])
        if _git_head(temporary) != commit:
            raise WorkspaceError(f"clone of {url} did not resolve to {commit}")
        if path.exists():
            raise WorkspaceError(f"{path} appeared during clone; preserving existing path")
        os.replace(temporary, path)
    except Exception:
        # The temporary checkout is ours and only created because the target
        # was absent; leave it recoverable if cleanup itself fails.
        import shutil
        shutil.rmtree(temporary, ignore_errors=True)
        raise
    return {"path": str(path), "commit": commit, "pinned": True, "dirty": False, "cloned": True}


def local_storm_library() -> Path | None:
    candidates = (
        STORMLIB_BUILD / "storm.framework/storm",
        STORMLIB_BUILD / "libstorm.so",
        STORMLIB_BUILD / "libstorm.dylib",
        STORMLIB_BUILD / "storm.dll",
        STORMLIB_BUILD / "libStormLib.so",
        STORMLIB_BUILD / "libStormLib.dylib",
    )
    return next((candidate for candidate in candidates if candidate.is_file()), None)


def ensure_stormlib() -> dict:
    library = local_storm_library()
    built = False
    if library is None:
        STORMLIB_BUILD.mkdir(parents=True, exist_ok=True)
        _run(["cmake", "-S", str(STORMLIB_PATH), "-B", str(STORMLIB_BUILD),
              "-DBUILD_SHARED_LIBS=ON", "-DSTORM_BUILD_TESTS=OFF",
              "-DSTORM_SKIP_INSTALL=ON", "-DCMAKE_BUILD_TYPE=Release"])
        _run(["cmake", "--build", str(STORMLIB_BUILD), "--config", "Release", "--parallel"])
        built = True
        library = local_storm_library()
    if library is None:
        raise WorkspaceError(f"StormLib build completed without a recognized shared library in {STORMLIB_BUILD}")
    return {"path": str(library), "present": True, "built": built}


def file_info(path: Path, expected_sha256: str | None = None) -> dict:
    info = {"path": str(path), "present": path.is_file()}
    if not path.is_file():
        if expected_sha256:
            info["expected_sha256"] = expected_sha256
        return info
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
            size += len(chunk)
    actual = digest.hexdigest()
    info.update({"size_bytes": size, "sha256": actual})
    if expected_sha256:
        info.update({"expected_sha256": expected_sha256, "verified": actual == expected_sha256})
    return info


def _json_file(path: Path) -> dict:
    if not path.is_file():
        return {}
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}


def status() -> dict:
    refs = {}
    for label, path, pin in (("d2decomped", D2DECOMP_PATH, D2DECOMP_COMMIT),
                             ("stormlib", STORMLIB_PATH, STORMLIB_COMMIT)):
        head = _git_head(path)
        refs[label] = {"path": str(path), "commit": head, "expected_commit": pin,
                       "pinned": head == pin if head else False}
        if head:
            refs[label]["dirty"] = bool(subprocess.check_output(
                ["git", "-C", str(path), "status", "--porcelain"], text=True).strip())
    download_manifest = _json_file(DOWNLOAD_MANIFEST)
    recorded_demo = next((item for item in download_manifest.get("assets", [])
                          if item.get("filename") == DEMO_PATH.name), {})
    installer = file_info(DEMO_PATH, DEMO_SHA256)
    if recorded_demo:
        installer["manifest_sha256"] = recorded_demo.get("sha256")
        installer["manifest_matches"] = installer.get("sha256") == recorded_demo.get("sha256")
        installer["download_url"] = recorded_demo.get("url")
    asset_manifest = _json_file(ASSET_MANIFEST)
    header = file_info(ASSET_HEADER)
    importer = file_info(IMPORTER_PATH)
    header["manifest_present"] = ASSET_MANIFEST.is_file()
    header["source_sha256"] = asset_manifest.get("source_sha256")
    header["reader_commit"] = asset_manifest.get("reader_commit")
    header["stormlib"] = asset_manifest.get("stormlib")
    header["schema_version"] = asset_manifest.get("schema_version")
    header["importer_sha256"] = asset_manifest.get("importer_sha256")
    header["manifest_header_sha256"] = asset_manifest.get("header_sha256")
    header["importer_matches"] = asset_manifest.get("importer_sha256") == importer.get("sha256")
    header["header_matches_manifest"] = asset_manifest.get("header_sha256") == header.get("sha256")
    header["schema_matches"] = asset_manifest.get("schema_version") == ASSET_SCHEMA_VERSION
    header["matches_demo"] = (header.get("present") and header.get("source_sha256") == DEMO_SHA256
                              and header.get("reader_commit") == D2DECOMP_COMMIT
                              and asset_manifest.get("stormlib", "").endswith(STORMLIB_COMMIT)
                              and header["schema_matches"] and header["importer_matches"]
                              and header["header_matches_manifest"])
    header["current_importer_sha256"] = importer.get("sha256")
    rom = file_info(ROM_PATH)
    library = local_storm_library()
    refs_pinned = all(ref.get("pinned") for ref in refs.values())
    storm_present = library is not None
    installer_verified = installer.get("verified", False)
    town_report = _json_file(ASSET_DIR / "town-source-report.json")
    extra_manifest = _json_file(EXTRA_MANIFEST)
    extras_current = all(extra_manifest.get(str(path.relative_to(ROOT))) == file_info(path).get("sha256")
                         and path.is_file() for path in EXTRA_FILES)
    item_ready = ITEM_HEADER.is_file() and extras_current
    town_ready = TOWN_HEADER.is_file() and town_report.get("core_ready", False) and extras_current
    asset_ready = bool(header["matches_demo"]) and item_ready and town_ready
    return {
        "references": refs,
        "stormlib_build": {"path": str(library) if library else str(STORMLIB_BUILD),
                            "present": storm_present},
        "installer": installer,
        "assets": header,
        "items": file_info(ITEM_HEADER),
        "town": {"header": file_info(TOWN_HEADER), "core_ready": town_ready},
        "extras_current": extras_current,
        "rom": rom,
        "prepared": refs_pinned and storm_present and asset_ready,
        "rom_ready": rom["present"],
        "ready": refs_pinned and storm_present and asset_ready and rom["present"],
        "source_dir": str(SOURCE_DIR),
        "download_manifest": {"path": str(DOWNLOAD_MANIFEST),
                              "present": DOWNLOAD_MANIFEST.is_file()},
    }



def extract_item_tables(archives: list[Path], output_dir: Path | None = None) -> list[Path]:
    """Copy only the three authored-packer inputs from the verified demo MPQs."""
    if not archives:
        raise WorkspaceError(f"no demo MPQ archives were extracted to {SOURCE_DIR}")
    if output_dir is None:
        output_dir = ITEM_TABLE_DIR
    output_dir.mkdir(parents=True, exist_ok=True)
    extracted = []
    for member in ITEM_TABLE_MEMBERS:
        destination = output_dir / member.rsplit("\\", 1)[-1]
        contents = None
        for archive in archives:
            try:
                contents = d2_import.extract_mpq_member(archive, member)
                break
            except FileNotFoundError:
                continue
        if contents is None:
            names = ", ".join(path.name for path in archives)
            raise WorkspaceError(f"required demo table {member} was not found in extracted archives: {names}")
        destination.write_bytes(contents)
        extracted.append(destination)
    return extracted


def prepare_demo_sources() -> tuple[list[Path], list[Path]]:
    """Verify/unpack the demo MPQs and refresh the exact item-table inputs."""
    _run([sys.executable, str(TOOLS / "d2_fetch_demo.py"), "--source-dir", str(SOURCE_DIR)])
    archives = sorted(path for path in SOURCE_DIR.iterdir()
                      if path.is_file() and path.suffix.lower() == ".mpq")
    tables = extract_item_tables(archives, ITEM_TABLE_DIR)
    return archives, tables


def prepare() -> dict:
    d2_ref = ensure_checkout(D2DECOMP_PATH, D2DECOMP_URL, D2DECOMP_COMMIT)
    storm_ref = ensure_checkout(STORMLIB_PATH, STORMLIB_URL, STORMLIB_COMMIT)
    storm = ensure_stormlib()
    source_archives, _item_tables = prepare_demo_sources()

    existing_assets = _json_file(ASSET_MANIFEST)
    current_importer = file_info(IMPORTER_PATH).get("sha256")
    current_header = file_info(ASSET_HEADER).get("sha256")
    assets_are_current = (ASSET_HEADER.is_file()
                          and existing_assets.get("source_sha256") == DEMO_SHA256
                          and existing_assets.get("reader_commit") == D2DECOMP_COMMIT
                          and existing_assets.get("stormlib", "").endswith(STORMLIB_COMMIT)
                          and existing_assets.get("schema_version") == ASSET_SCHEMA_VERSION
                          and existing_assets.get("importer_sha256") == current_importer
                          and existing_assets.get("header_sha256") == current_header)
    imported = False
    if not assets_are_current:
        _run([sys.executable, str(TOOLS / "d2_import.py"), "--den-demo", str(DEMO_PATH)])
        imported = True
    _run([sys.executable, str(TOOLS / "d2_items_pack.py")])
    _run([sys.executable, str(TOOLS / "d2_town_assets.py"),
          *[arg for archive in source_archives
            for arg in ("--mpq", str(archive))]])
    EXTRA_MANIFEST.write_text(json.dumps({str(path.relative_to(ROOT)): file_info(path).get("sha256")
                                          for path in EXTRA_FILES}, indent=2) + "\n")
    result = status()
    result.update({"operation": "prepare", "cloned": {
        "d2decomped": d2_ref["cloned"], "stormlib": storm_ref["cloned"]},
        "stormlib_built": storm["built"], "download_checked": True,
        "source_archives": len(source_archives), "item_tables_extracted": len(_item_tables),
        "assets_imported": imported})
    if not result["prepared"]:
        raise WorkspaceError("asset workspace preparation finished with incomplete prerequisites: "
                             + json.dumps(result, sort_keys=True))
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("prepare", "status"))
    args = parser.parse_args(argv)
    try:
        result = prepare() if args.command == "prepare" else status()
    except WorkspaceError as error:
        print(json.dumps({"error": str(error)}, indent=2), file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
