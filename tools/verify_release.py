#!/usr/bin/env python3
"""Independently extract and verify release contents using Lhasa and tarfile.
SPDX-License-Identifier: GPL-2.0-only
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile


def check_manifest(base, path):
    checked = set()
    for line in path.read_text().splitlines():
        expected, name = line.split(maxsplit=1)
        relative = Path(name)
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError(f'unsafe manifest path: {name}')
        data = (base / relative).read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError(f'checksum mismatch: {name}')
        checked.add(name)
    return checked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    dist = args.directory.resolve()
    checked = check_manifest(dist, dist / 'SHA256SUMS')
    info = json.loads((dist / 'build-info.json').read_text())
    prefix = f'Prism2Flash-{info["version"]}'
    native = f'{prefix}-amiga-68000.lha'
    source = f'{prefix}-source.tar.gz'
    if checked != {native, source, 'build-info.json', 'RELEASE-NOTES.md'}:
        raise ValueError('release manifest has missing or unexpected assets')
    with tempfile.TemporaryDirectory(prefix='prism2-verify-') as temp:
        temp = Path(temp)
        subprocess.run(['lha', 'xfq', str(dist / native)], cwd=temp, check=True)
        bundle = temp / prefix
        bundled = check_manifest(bundle, bundle / 'SHA256SUMS')
        required = {'C/Prism2Genesis', 'C/Prism2Firmware', 'C/Prism2Flash', 'Libs/pccard.library', 'COPYING', 'SOURCE.txt',
                    'third_party/pccard/pccard.lha', 'third_party/pccard/GPL',
                    'third_party/pccard/LGPL', 'third_party/NOTICES', 'docs/INSTALL.md'}
        if not required <= bundled:
            raise ValueError('binary package lacks required executable, source archive or notices')
        if hashlib.sha256((bundle / 'C/Prism2Genesis').read_bytes()).hexdigest() != info['binary_sha256']:
            raise ValueError('binary differs from build-info.json')
        if set(info.get('binaries', {})) != {'Prism2Genesis', 'Prism2Firmware', 'Prism2Flash'}:
            raise ValueError('metadata must describe all three native executables')
        for name, record in info['binaries'].items():
            data = (bundle / 'C' / name).read_bytes()
            if hashlib.sha256(data).hexdigest() != record['sha256'] or len(data) != record['bytes']:
                raise ValueError(f'{name} differs from build metadata')
        if (bundle / 'build-info.json').read_bytes() != (dist / 'build-info.json').read_bytes():
            raise ValueError('bundled metadata differs')
        if hashlib.sha256((dist / source).read_bytes()).hexdigest() != info['source_archive_sha256']:
            raise ValueError('source archive differs from build-info.json')
        with tarfile.open(dist / source) as archive:
            names = set(archive.getnames())
            for name in ('COPYING', 'VERSION', 'src/main.cpp', 'src/prism2.cpp', 'src/amiga.cpp',
                         'src/firmware.cpp', 'src/fw_validate.cpp', 'src/firmware_main.cpp', 'src/pda.cpp', 'include/firmware.h',
                         'src/flash_main.cpp', 'src/host_flash.cpp', 'src/bootstrap.cpp', 'src/flash_plan.cpp',
                         'src/backup.cpp', 'src/sha256.cpp', 'include/owned_buffer.h', 'include/storage.h', 'include/owned_memory.h', 'include/file.h',
                         '.clang-format', 'tools/format.py', 'CMakeLists.txt', 'tools/ci.sh', 'tools/toolchain.json',
                         'third_party/pccard/pccard.lha', 'third_party/hostap/hostap-utils-0.4.7.tar.gz'):
                if f'{prefix}/{name}' not in names:
                    raise ValueError(f'corresponding source is missing {name}')
            version = archive.extractfile(f'{prefix}/VERSION').read().decode().strip()
            source_info = json.load(archive.extractfile(f'{prefix}/SOURCE.json'))
            if version != info['version'] or source_info['commit'] != info['commit']:
                raise ValueError('source version or commit differs from binary metadata')
    print(f'Verified release assets, extracted binary package and corresponding source: {info["version"]}')


if __name__ == '__main__':
    main()
