#!/usr/bin/env python3
"""Package the native 68000 executable, complete source and SHA-256 manifest.

LHA uses the universally supported stored (-lh0-) level-0 format. This keeps
packaging identical on Linux/macOS without requiring an external LHA writer.
SPDX-License-Identifier: GPL-2.0-only
"""
import argparse
import datetime
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import tarfile

from check_project import ROOT, metadata


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args])


def sha(data):
    return hashlib.sha256(data).hexdigest()


def crc16(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xa001 if crc & 1 else 0)
    return crc


def write_lha(path, files, date):
    """Write stored entries, each with a header checksum and CRC-16/ARC."""
    dos_time = ((date.year - 1980) << 25) | (date.month << 21) | (date.day << 16)
    with path.open('wb') as archive:
        for name, data in sorted(files.items()):
            name = name.encode('ascii')
            if len(name) > 233 or len(data) >= 2**32:
                raise ValueError('file exceeds level-0 LHA limits')
            header = (b'-lh0-' + struct.pack('<III', len(data), len(data), dos_time)
                      + b'\x20\x00' + bytes([len(name)]) + name
                      + struct.pack('<H', crc16(data)))
            archive.write(bytes([len(header), sum(header) & 255]) + header + data)
        archive.write(b'\x00')


def source_archive(path, prefix, files, epoch, commit):
    with path.open('wb') as raw, gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=epoch) as gz:
        with tarfile.open(fileobj=gz, mode='w', format=tarfile.PAX_FORMAT) as archive:
            entries = {name: ((ROOT / name).read_bytes(),
                             0o755 if os.access(ROOT / name, os.X_OK) else 0o644)
                       for name in files}
            entries['SOURCE.json'] = (json.dumps({'commit': commit, 'source_date_epoch': epoch},
                                                indent=2).encode() + b'\n', 0o644)
            for name, (data, mode) in sorted(entries.items()):
                info = tarfile.TarInfo(f'{prefix}/{name}')
                info.size, info.mode, info.mtime = len(data), mode, epoch
                info.uid = info.gid = 0
                info.uname = info.gname = ''
                archive.addfile(info, io.BytesIO(data))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build-amiga')
    parser.add_argument('--output', type=Path, default=ROOT / 'dist')
    parser.add_argument('--allow-dirty', action='store_true', help='local packaging checks only')
    args = parser.parse_args()
    version, date = metadata()
    subprocess.run([os.sys.executable, str(ROOT / 'tools/check_project.py')], check=True)
    dirty = bool(git('status', '--porcelain'))
    if dirty and not args.allow_dirty:
        parser.error('release packaging requires a clean checkout')
    files = git('ls-files', '-z').decode().rstrip('\0').split('\0')
    if any((ROOT / name).is_symlink() for name in files):
        parser.error('source archive does not accept symlinks')
    commit = git('rev-parse', 'HEAD').decode().strip()
    epoch = int(git('show', '-s', '--format=%ct', 'HEAD'))
    build = args.build_dir.resolve()
    binaries = {name: (build / name).read_bytes() for name in ('Prism2Genesis', 'Prism2Firmware')}
    for name, binary in binaries.items():
        if binary[:4] != b'\x00\x00\x03\xf3':
            parser.error(f'{name} is not an Amiga HUNK executable')
        expected = f'$VER: {name} {version.split(".")[0]}.{version.split(".")[1]} '
        if expected.encode() not in binary or f'{name} {version}'.encode() not in binary:
            parser.error(f'{name} version differs from VERSION')
        if b'mathieeedoubbas.library' in binary or b'mathieeedoubtrans.library' in binary:
            parser.error(f'{name} includes the unwanted double-math startup dependency')
    config = json.loads((build / 'generated/build-config.json').read_text())
    if config['version'] != version:
        parser.error('build configuration version differs from VERSION')
    compiler = config['compiler']
    compiler_version = subprocess.check_output([compiler, '--version'], text=True).splitlines()[0]
    prefix = f'Prism2Flash-{version}'
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = output / f'{prefix}-source.tar.gz'
    source_archive(source, prefix, files, epoch, commit)
    info = {
        'project': 'Prism2Flash', 'version': version, 'release_date': date.isoformat(),
        'commit': commit, 'source_date_epoch': epoch, 'source_dirty': dirty,
        'target': 'm68k-amigaos', 'cpu': '68000', 'minimum_os': 'AmigaOS 2.05',
        'compiler': compiler_version, 'compiler_pin': json.loads((ROOT / 'tools/toolchain.json').read_text()),
        'binary_sha256': sha(binaries['Prism2Genesis']),
        'binaries': {name: {'sha256': sha(data), 'bytes': len(data)} for name, data in binaries.items()},
        'source_archive_sha256': sha(source.read_bytes()),
        'hardware_tested': False,
        'scope': 'Native active operations blocked pending mandatory full-flash backups; offline firmware validation; no flash programming',
    }
    info_bytes = (json.dumps(info, indent=2) + '\n').encode()
    (output / 'build-info.json').write_bytes(info_bytes)
    bundle = {f'{prefix}/C/{name}': data for name, data in binaries.items()}
    bundle[f'{prefix}/build-info.json'] = info_bytes
    include = ['README.md', 'COPYING', 'CHANGELOG.md', 'ROADMAP.md', 'VERSION', 'RELEASE_DATE',
               'docs/INSTALL.md', 'docs/HARDWARE-TESTING.md', 'docs/PROTOCOL.md', 'docs/SOURCES.md',
               'docs/EMULATOR-RESULTS.md', 'docs/OFFLINE-FIRMWARE.md', 'docs/FIRMWARE-FINDINGS.md', 'third_party/NOTICES', 'third_party/AmiNetXDuo-LICENSE',
               'third_party/pccard/GPL', 'third_party/pccard/LGPL', 'third_party/pccard/pccard.lha',
               'third_party/pccard/Libs/pccard.library']
    for name in include:
        bundle[f'{prefix}/{name}'] = (ROOT / name).read_bytes()
    bundle[f'{prefix}/Libs/pccard.library'] = (ROOT / 'third_party/pccard/Libs/pccard.library').read_bytes()
    bundle[f'{prefix}/SOURCE.txt'] = (
        f'Complete corresponding GPL source: {source.name}\n'
        f'https://github.com/tinic/Prism2Flash/releases/tag/v{version}\n'
        f'Commit: {commit}\nNo firmware image is included. Read docs/INSTALL.md before use.\n'
    ).encode()
    manifest = ''.join(f'{sha(data)}  {name[len(prefix)+1:]}\n' for name, data in sorted(bundle.items()))
    bundle[f'{prefix}/SHA256SUMS'] = manifest.encode()
    native = output / f'{prefix}-amiga-68000.lha'
    write_lha(native, bundle, date)
    notes = (ROOT / 'CHANGELOG.md').read_text().split(f'## [{version}] - {date.isoformat()}\n', 1)[1]
    notes = notes.split('\n## [', 1)[0].strip()
    (output / 'RELEASE-NOTES.md').write_text(
        f'# Prism2Flash {version}\n\n{notes}\n\n'
        f'## Downloads\n\n- `{native.name}`: Amiga executables, pccard.library and documentation.\n'
        f'- `{source.name}`: complete corresponding source and build tools.\n'
        '- `SHA256SUMS`: download integrity checks.\n- `build-info.json`: compiler, source and binary metadata.\n\n'
        f'Commit: `{commit}`. This release has not been tested with a real card.\n')
    assets = [native, source, output / 'build-info.json', output / 'RELEASE-NOTES.md']
    (output / 'SHA256SUMS').write_text(''.join(f'{sha(p.read_bytes())}  {p.name}\n' for p in assets))
    print(f'Packaged {version}: {native.name}, {source.name}')


if __name__ == '__main__':
    main()
