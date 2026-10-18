#!/usr/bin/env python3
"""Tamper every saved raw file after capture verification, before comparison.
SPDX-License-Identifier: GPL-2.0-only
"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix='p2-journal-') as temp:
    for name in ('before-read-1.bin', 'before-read-2.bin',
                 'after-read-1.bin', 'after-read-2.bin'):
        out = Path(temp) / f'{name}-20261018T104000Z'
        result = subprocess.run([sys.argv[1], str(out), str(out / name)])
        assert result.returncode == 0, (name, result)
        report = json.loads((out / 'transaction.json').read_text())
        assert report['comparison_error'] and not report['success']
print('All four saved raw files independently reject post-verification tampering')
