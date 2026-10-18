#!/usr/bin/env python3
"""Validate release metadata and retained third-party checksums.
SPDX-License-Identifier: GPL-2.0-only
"""
import argparse
import datetime
import hashlib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent


def metadata():
    version = (ROOT / 'VERSION').read_text().strip()
    # Restrict to SemVer without build metadata: every public version has one tag.
    number = r'(?:0|[1-9][0-9]*)'
    identifier = rf'(?:{number}|[0-9]*[A-Za-z-][0-9A-Za-z-]*)'
    pattern = rf'{number}\.{number}\.{number}(?:-{identifier}(?:\.{identifier})*)?'
    if not re.fullmatch(pattern, version):
        raise ValueError('VERSION is not canonical SemVer')
    date = datetime.date.fromisoformat((ROOT / 'RELEASE_DATE').read_text().strip())
    return version, date


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', help='require a release tag matching VERSION')
    args = parser.parse_args()
    version, date = metadata()
    if args.tag and args.tag != f'v{version}':
        parser.error(f'tag must be v{version}, got {args.tag}')
    changelog = (ROOT / 'CHANGELOG.md').read_text()
    if f'## [{version}] - {date.isoformat()}' not in changelog:
        raise ValueError('CHANGELOG.md needs a dated entry for VERSION')
    for line in (ROOT / 'third_party/SHA256SUMS').read_text().splitlines():
        expected, name = line.split(maxsplit=1)
        actual = hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
        if actual != expected:
            raise ValueError(f'third-party checksum mismatch: {name}')
    print(f'Metadata, changelog and third-party checksums verified: {version}')


if __name__ == '__main__':
    main()
