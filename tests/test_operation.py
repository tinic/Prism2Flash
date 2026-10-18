#!/usr/bin/env python3
"""Independent filesystem checks of the model-only owned operation transaction.
SPDX-License-Identifier: GPL-2.0-only
"""
import hashlib
import json
import os
from pathlib import Path
import resource
import signal
import subprocess
import sys
import tempfile

binary = sys.argv[1]
stamp = '20261018T104000Z'
with tempfile.TemporaryDirectory(prefix='p2-operation-') as temp:
    root = Path(temp)
    for name, command in [('inspect', ['inspect']),
                          ('probe8', ['probe', '--sram', '8']),
                          ('probe16', ['probe', '--sram', '16'])]:
        out = root / f'{name}-{stamp}'
        args = [binary, *command, '--output', str(out), '--utc', stamp]
        run = subprocess.run(args, capture_output=True, text=True)
        assert run.returncode == 0, run
        report = json.loads((out / 'transaction.json').read_text())
        assert report['simulation_only'] and not report['hardware_authorization']
        assert report['success'] and report['operation_started'] and report['readback_attempted']
        digests = []
        for phase in ('before', 'after'):
            manifest = json.loads((out / f'{phase}.json').read_text())
            assert manifest['phase'] == phase
            assert manifest['files_reopened_and_verified']
            assert manifest['range'] == [0, 0x800000] and manifest['selector'] == 1
            assert manifest['profile'] == 'protocol-model-nv-linear-8m-v1'
            assert manifest['hardware_authorization'] is False
            for index in (1, 2):
                data = (out / f'{phase}-read-{index}.bin').read_bytes()
                assert len(data) == 0x800000
                digest = hashlib.sha256(data).hexdigest()
                assert digest == manifest['sha256'][index - 1]
                digests.append(digest)
        assert len(set(digests)) == 1
        # A prior completed backup cannot authorize or overwrite a second run.
        old_manifest = (out / 'transaction.json').read_bytes()
        repeat = subprocess.run(args, capture_output=True, text=True)
        assert repeat.returncode == 20
        assert (out / 'transaction.json').read_bytes() == old_manifest
    # Real short file writes, beyond logical fault injection. Ignore SIGXFSZ so
    # stdio reports EFBIG and the transaction can record the failed acquisition.
    def restrict_file_size():
        signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
        resource.setrlimit(resource.RLIMIT_FSIZE, (4096, 4096))
    limited = root / f'storage-failure-{stamp}'
    run = subprocess.run([binary, 'probe', '--sram', '16', '--output', str(limited),
                          '--utc', stamp], capture_output=True, text=True,
                         preexec_fn=restrict_file_size)
    assert run.returncode == 20, run
    report = json.loads((limited / 'transaction.json').read_text())
    assert not report['success'] and not report['operation_started']
    assert not report['readback_attempted'] and report['acquisition_error']
    assert (limited / 'before-read-1.bin').stat().st_size == 4096
    assert (limited / 'after-read-1.bin').stat().st_size == 0
    # Real descriptor exhaustion after mkdir: a failure journal must survive,
    # and no acquisition or operation may have started.
    def restrict_descriptors():
        resource.setrlimit(resource.RLIMIT_NOFILE, (5, 5))
    limited_open = root / f'open-failure-{stamp}'
    run = subprocess.run([binary, 'inspect', '--output', str(limited_open),
                          '--utc', stamp], capture_output=True, text=True,
                         preexec_fn=restrict_descriptors)
    assert run.returncode == 20, run
    report = json.loads((limited_open / 'transaction.json').read_text())
    assert report['preparation_error'] and not report['operation_started']
    assert not report['readback_attempted'] and not report['success']
    assert (limited_open / 'before-read-1.bin').stat().st_size == 0
    changed = root / f'changed-nv-{stamp}'
    run = subprocess.run([binary, 'probe', '--sram', '16', '--output', str(changed),
                          '--utc', stamp], capture_output=True, text=True,
                         env={**os.environ, 'PRISM2_SIM_NV_CHANGE': '1'})
    assert run.returncode == 20, run
    report = json.loads((changed / 'transaction.json').read_text())
    assert report['operation_started'] and report['readback_attempted']
    assert not report['success'] and report['comparison_error']
    assert not report['operation_error'] and not report['readback_error']
    # Each phase's repeated files match; the unexpected cross-phase byte
    # difference must still fail and be retained, without an automatic restore.
    before = (changed / 'before-read-1.bin').read_bytes()
    after = (changed / 'after-read-1.bin').read_bytes()
    assert before[0] ^ after[0] == 1 and before[1:] == after[1:]
    assert after == (changed / 'after-read-2.bin').read_bytes()
print('Model file transactions: fresh dual before/after captures, independent hashes, '
      'no stale reuse/overwrite, and real storage failure gate passed')
