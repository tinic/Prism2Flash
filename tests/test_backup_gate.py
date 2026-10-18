#!/usr/bin/env python3
"""Verify impactful commands refuse before any backend access.
SPDX-License-Identifier: GPL-2.0-only
"""
import subprocess
import sys

binary = sys.argv[1]
for args in (['inspect'], ['probe', '--sram', '8'], ['probe', '--sram', '16']):
    result = subprocess.run([binary, *args], capture_output=True, text=True)
    assert result.returncode == 20, (args, result)
    assert not result.stdout, (args, result)
    assert 'verified timestamped full-flash backup is required' in result.stderr
    assert 'no card access was attempted' in result.stderr
for arg in ('--help', '--version'):
    result = subprocess.run([binary, arg], capture_output=True, text=True)
    assert result.returncode == 0 and not result.stderr, (arg, result)
print('Mandatory-backup gate: three active commands refused before backend access; help/version passed')
