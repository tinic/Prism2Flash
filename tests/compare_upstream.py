#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Compare approved private images with the retained Host AP parser.

Only known SHA-256-pinned images are accepted: the legacy parser has unsafe
malformed-input paths and is never used as our rejection oracle. No firmware
is bundled or downloaded. Both harnesses are host-only and contain no card I/O.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def dump(reference):
    im = 's' if reference else 'im'
    fields = [('I', 'component.component' if reference else 'component.id',
               'component.variant', 'component.major', 'component.minor',
               'start_addr' if reference else 'entry')]
    code = ['puts("CANONICAL");']
    for label, *names in fields:
        code.append('printf("' + label + ' ' + ' '.join(['%lu'] * len(names)) + '\\n",' +
                    ','.join(f'(unsigned long){im}->{v}' for v in names) + ');')
    arrays = [
        ('P', 'platform_count', 'platforms', ['platform' if reference else 'id', 'variant', 'major', 'minor']),
        ('R', 'compatibility_count' if reference else 'range_count',
         'compatibilities' if reference else 'ranges', ['role', 'iface_id' if reference else 'interface_id', 'variant', 'bottom', 'top']),
        ('L', 'plug_count', 'plugs', ['pdr', 'plug_addr' if reference else 'address', 'plug_len' if reference else 'length']),
        ('C', 'crc16_count' if reference else 'crc_count', 'crc16' if reference else 'crcs',
         ['start', 'len' if reference else 'length', 'prog' if reference else 'program']),
    ]
    for label, count, array, names in arrays:
        code.append(f'for(i=0;i<(size_t){im}->{count};++i) printf("{label} ' +
                    ' '.join(['%lu'] * len(names)) + '\\n",' +
                    ','.join(f'(unsigned long){im}->{array}[i].{v}' for v in names) + ');')
    count = 's3_used' if reference else 'segment_count'
    array = 's3_records' if reference else 'segments'
    address, length = ('addr', 'len') if reference else ('address', 'length')
    code.append(f'for(i=0;i<(size_t){im}->{count};++i) {{ printf("S %lu %lu ",'
                f'(unsigned long){im}->{array}[i].{address},(unsigned long){im}->{array}[i].{length});'
                f'for(j=0;j<(size_t){im}->{array}[i].{length};++j) printf("%02x",{im}->{array}[i].data[j]); puts(""); }}')
    result = '\n'.join(code)
    if not reference:
        for field in ('component', 'entry', 'platforms', 'ranges', 'plugs', 'segments'):
            result = result.replace('im->' + field, 'im->' + field + '()')
        for count, array in [('platform_count', 'platforms'), ('range_count', 'ranges'),
                             ('plug_count', 'plugs'), ('crc_count', 'signatures'),
                             ('segment_count', 'segments')]:
            result = result.replace('im->' + count, 'im->' + array + '().size()')
        result = result.replace('im->crcs', 'im->signatures()')
        result = result.replace('segments()[i].length', 'segments()[i].data.size()')
        result = result.replace('segments()[i].data[j]', 'segments()[i].data.bytes()[j]')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--images', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build-host')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    pins = json.loads((ROOT / 'docs/evidence/firmware-images.json').read_text())['images']
    for image in pins:
        raw = (args.images / image['name']).read_bytes()
        if hashlib.sha256(raw).hexdigest() != image['sha256']:
            parser.error(f'private image differs from approved hash: {image["name"]}')
    with tarfile.open(ROOT / 'third_party/hostap/hostap-utils-0.4.7.tar.gz') as archive:
        name = next(n for n in archive.getnames() if n.endswith('/prism2_srec.c'))
        source = archive.extractfile(name).read().decode()
    start = source.index('static int verbose = 0;')
    end = source.index('enum { ROLE_SUPPLIER = 0, ROLE_ACTOR = 1 };')
    # This prefix contains just image processing, before Linux device routines.
    legacy = source[start:end]
    unused_start = legacy.index('struct wlan_info {')
    unused_end = legacy.index('\n};', unused_start) + 3
    legacy = legacy[:unused_start] + legacy[unused_end:]
    source = '#include <errno.h>\n#include <stdlib.h>\n#include <stdio.h>\n#include <string.h>\n#include <assert.h>\n' + legacy
    source += '\nint main(int argc,char **argv) { size_t i,j;struct srec_data *s; if(argc!=2)return 20; s=read_srec(argv[1]); if(!s)return 20;\n' + dump(True) + '\nfree_srec(s);return 0;}\n'
    current = '#include "firmware.h"\n#include "file.h"\nint main(int argc,char **argv) {size_t i,j;if(argc!=2)return 20;auto file=p2::File::open(argv[1],"rb");if(!file)return 20;auto image=p2::firmware::Image::read(*file);if(!image)return 20;const auto *im=&*image;\n' + dump(False) + '\nreturn 0;}\n'
    build = args.build_dir.resolve()
    results = []
    with tempfile.TemporaryDirectory(prefix='upstream-', dir=build) as tmp:
        tmp = Path(tmp)
        (tmp / 'reference.c').write_text(source)
        (tmp / 'current.cpp').write_text(current)
        compiler = os.environ.get('CC', 'cc')
        subprocess.run([compiler, '-std=gnu99', '-w', str(tmp / 'reference.c'), '-o', str(tmp / 'reference')], check=True)
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++23', '-I'+str(ROOT / 'include'), str(tmp / 'current.cpp'),
                        str(build / 'libprism2_firmware.a'), str(build / 'libprism2_data.a'),
                        '-o', str(tmp / 'current')], check=True)
        for image in pins:
            name = str((args.images / image['name']).resolve())
            outputs = [subprocess.check_output([str(tmp / binary), name], timeout=15).split(b'CANONICAL\n', 1)[1]
                       for binary in ('reference', 'current')]
            if outputs[0] != outputs[1]:
                raise RuntimeError('upstream parsed metadata/payload differs: '+image['name'])
            results.append({'name': image['name'], 'image_sha256': image['sha256'],
                            'canonical_sha256': hashlib.sha256(outputs[0]).hexdigest(),
                            'metadata_and_payload_equal': True})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({'reference': 'Host AP utilities 0.4.7 parser prefix; no device code',
                                     'comparisons': results, 'hardware_tested': False}, indent=2)+'\n')
    print(f'All {len(results)} private images match upstream parsed metadata and payload bytes.')


if __name__ == '__main__':
    main()
