#!/usr/bin/env python3
"""Format project-owned C++ with the pinned LLVM style. Leave upstream files alone.
SPDX-License-Identifier: GPL-2.0-only
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
VERSION = '18.1.8'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    formatter = os.environ.get('CLANG_FORMAT') or shutil.which('clang-format')
    if not formatter:
        parser.error(f'install clang-format=={VERSION}, or set CLANG_FORMAT')
    version = subprocess.check_output([formatter, '--version'], text=True)
    if f'version {VERSION}' not in version:
        parser.error(f'clang-format {VERSION} required; got {version.strip()}')
    paths = sorted(path for directory in ('src', 'include', 'tests')
                   for path in (ROOT / directory).iterdir()
                   if path.suffix in ('.cpp', '.h') or path.name == 'version.h.in')
    flags = ['--dry-run', '--Werror'] if args.check else ['-i']
    subprocess.run([formatter, '--style=file', *flags, *map(str, paths)], check=True)
    print(f'LLVM formatting {"checked" if args.check else "applied"}: {len(paths)} files')


if __name__ == '__main__':
    main()
