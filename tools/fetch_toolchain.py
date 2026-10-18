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
    cache = Path(os.environ.get('PRISM2_TOOLCHAIN_CACHE', Path.home() / '.cache/prism2flash/toolchain'))
    root = cache.resolve() / asset['sha256'][:12]
    if args.print_root:
        print(root)
        return
    compiler = root / 'bin/m68k-amigaos-gcc'
    if not compiler.is_file():
        cache.mkdir(parents=True, exist_ok=True)
        name = f"m68k-amigaos-gcc-{pin['asset_version']}-ndk3.9-{asset['name']}.tar.xz"
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
                            'opt/m68k-amigaos'], check=True, stdout=sys.stderr)
            extracted = temp / 'opt/m68k-amigaos'
            if not (extracted / 'm68k-amigaos/ndk-include/exec/types.h').is_file():
                raise RuntimeError('toolchain has no NDK headers')
            shutil.move(str(extracted), root)
    for script, options in [('fix-toolchain-crt0.py', []),
                            ('fix-toolchain-crt0.py', ['--check']),
                            ('fix-toolchain-inline-const.py', []),
                            ('fix-toolchain-inline-const.py', ['--check']),
                            ('check-toolchain-malloc.py', [])]:
        subprocess.run([sys.executable, str(TOOLS / script), str(root), *options],
                       check=True, stdout=sys.stderr)
    version = subprocess.check_output([str(compiler), '-dumpversion'], text=True).strip()
    if version != pin['gcc_version']:
        raise RuntimeError(f'expected GCC {pin["gcc_version"]}, got {version}')
    print(root)


if __name__ == '__main__':
    main()
