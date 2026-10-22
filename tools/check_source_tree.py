#!/usr/bin/env python3
"""Reject game data, build outputs and credentials from the Git source index."""
from pathlib import PurePosixPath
import re
import subprocess
import sys

ALLOWED_SUFFIXES = {'.c', '.h', '.s', '.ld', '.py', '.md', '.json', '.yml', '.yaml', '.txt'}
ALLOWED_NAMES = {'Makefile', 'dev', 'LICENSE', 'COPYING', '.gitignore', '.gitattributes'}
FORBIDDEN_DIRS = {'.local', 'build', 'references', 'generated', 'extracted', '__pycache__', 'node_modules', '.venv'}
FORBIDDEN_NAMES = {'d2_den_assets.h', 'd2_den_items.h', 'd2_town_assets.h'}
SECRET_PATTERNS = (
    re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----'),
    re.compile(rb'gh[pousr]_[A-Za-z0-9]{30,}'),
    re.compile(rb'github_pat_[A-Za-z0-9_]{30,}'),
)


def main():
    entries = subprocess.check_output(['git', 'ls-files', '--stage', '-z']).split(b'\0')
    failures = []
    count = 0
    for entry in entries:
        if not entry:
            continue
        meta, raw_path = entry.split(b'\t', 1)
        mode, oid, stage = meta.split()
        name = raw_path.decode('utf-8')
        path = PurePosixPath(name)
        count += 1
        if mode not in (b'100644', b'100755') or stage != b'0':
            failures.append(f'{name}: only regular, resolved source files are allowed')
            continue
        if any(part in FORBIDDEN_DIRS for part in path.parts):
            failures.append(f'{name}: local/generated directory is prohibited')
        if path.name in FORBIDDEN_NAMES or path.name.endswith(('_generated.h', '-trigger-report.json', '_pack_report.json')):
            failures.append(f'{name}: game-derived generated data is prohibited')
        if path.name.startswith('.env') or path.name.lower() in {'credentials', 'id_rsa', 'id_ed25519'}:
            failures.append(f'{name}: credential path is prohibited')
        if path.name not in ALLOWED_NAMES and path.suffix.lower() not in ALLOWED_SUFFIXES:
            failures.append(f'{name}: file type is outside the source allowlist')
        data = subprocess.check_output(['git', 'cat-file', 'blob', oid.decode('ascii')])
        if len(data) > 512 * 1024 or b'\0' in data:
            failures.append(f'{name}: binary or oversized payload is prohibited')
        if any(pattern.search(data) for pattern in SECRET_PATTERNS):
            failures.append(f'{name}: possible credential detected (value suppressed)')
    if not count:
        failures.append('No indexed source files found')
    if failures:
        print('\n'.join(failures), file=sys.stderr)
        return 1
    print(f'PASS: {count} indexed text source files; no game/build/credential payloads')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
