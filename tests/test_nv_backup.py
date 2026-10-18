#!/usr/bin/env python3
"""Independent output/hash checks for the model's full NV capture.
SPDX-License-Identifier: GPL-2.0-only
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

binary = sys.argv[1]
stamp = '20261018T104000Z'
with tempfile.TemporaryDirectory(prefix='p2-nv-test-') as temp:
    root = Path(temp)
    out = root / stamp
    bad = subprocess.run([binary, 'backup', '--output', str(out), '--utc', '20260229T104000Z'], capture_output=True, text=True)
    assert bad.returncode == 20 and not out.exists(), bad
    good = subprocess.run([binary, 'backup', '--output', str(out), '--utc', stamp], capture_output=True, text=True)
    assert good.returncode == 0, good
    report = json.loads((out / 'manifest.json').read_text())
    expected = b'\xff' * 0x7e0000 + bytes((i * 17 + (i >> 8) * 3) & 255 for i in range(0x7e0000, 0x800000))
    first = (out / 'nv-read-1.bin').read_bytes()
    second = (out / 'nv-read-2.bin').read_bytes()
    assert first == second == expected
    digest = hashlib.sha256(expected).hexdigest()
    assert report['sha256'] == [digest, digest]
    assert report['bytes_read'] == [0x800000, 0x800000]
    assert report['files_reopened_and_verified'] and report['nv_address_space_complete']
    assert not report['physical_flash_coverage_verified'] and not report['authorizes_impactful_operation']
    again = subprocess.run([binary, 'backup', '--output', str(out), '--utc', stamp], capture_output=True, text=True)
    assert again.returncode == 20
    assert (out / 'nv-read-1.bin').read_bytes() == first
print('Model capture matches independent byte/hash oracle; timestamp checks, manifest, repeat-file preservation passed')
