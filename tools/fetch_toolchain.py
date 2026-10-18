#!/usr/bin/env python3
"""Fetch the SHA-256 pinned compiler and repair/check its Amiga startup ABI.

Progress goes to stderr; stdout contains only the resolved compiler root.
SPDX-License-Identifier: GPL-2.0-only
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile

TOOLS = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--print-root', action='store_true', help='only report the cache path')
    args = parser.parse_args()
    pin = json.loads((TOOLS / 'toolchain.json').read_text())
    key = f'{platform.system()}-{platform.machine()}'
    if key not in pin['platforms']:
        parser.error(f'no pinned prebuilt for {key}; supply AMIGA_TOOLCHAIN_ROOT manually')
    asset = pin['platforms'][key]
    cache = Path(os.environ.get('PRISM2_TOOLCHAIN_CACHE', Path.home() / 'ai/toolchain-cache/prism2flash'))
    root = cache.resolve() / asset['sha256'][:12]
    if args.print_root:
        print(root)
        return
    compiler = root / 'bin/m68k-amigaos-gcc'
    if not compiler.is_file():
        cache.mkdir(parents=True, exist_ok=True)
        name = asset['asset']
        url = f"https://github.com/{pin['repository']}/releases/download/{pin['tag']}/{name}"
        with tempfile.TemporaryDirectory(prefix='prism2-tc-', dir=cache) as temp:
            temp = Path(temp)
            archive = temp / 'toolchain.tar.xz'
            subprocess.run(['curl', '-fL', '--retry', '3', '--connect-timeout', '20',
                            '-o', str(archive), url], check=True, stdout=sys.stderr)
            if hashlib.sha256(archive.read_bytes()).hexdigest() != asset['sha256']:
                raise RuntimeError('toolchain SHA-256 mismatch; refusing extraction')
            # This exact archive is pinned above; take only the compiler prefix.
            subprocess.run(['tar', 'xf', str(archive), '-C', str(temp),
                            pin['archive_root']], check=True, stdout=sys.stderr)
            extracted = temp / pin['archive_root']
            if not (extracted / 'm68k-amigaos/ndk-include/exec/types.h').is_file():
                raise RuntimeError('toolchain has no NDK headers')
            shutil.move(str(extracted), root)
    # RC17 already contains the repairs. Validate; never patch a new runtime.
    for script, options in [('fix-toolchain-crt0.py', ['--check']),
                            ('fix-toolchain-inline-const.py', ['--check']),
                            ('check-toolchain-malloc.py', [])]:
        subprocess.run([sys.executable, str(TOOLS / script), str(root), *options],
                       check=True, stdout=sys.stderr)
    version = subprocess.check_output([str(compiler), '-dumpfullversion'], text=True).strip()
    if version.rstrip('b') != pin['gcc_version']:
        raise RuntimeError(f'expected GCC {pin["gcc_version"]}, got {version}')
    print(root)


if __name__ == '__main__':
    main()
