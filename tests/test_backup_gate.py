#!/usr/bin/env python3
"""Verify impactful commands refuse before any backend access.
SPDX-License-Identifier: GPL-2.0-only
"""
import subprocess
import sys

binary = sys.argv[1]
commands = (['inspect'], ['probe', '--sram', '8'], ['probe', '--sram', '16'])
for args in (*commands, *(c + ['--output', 'Work:20261018T104000Z', '--utc',
                              '20261018T104000Z'] for c in commands)):
    result = subprocess.run([binary, *args], capture_output=True, text=True)
    assert result.returncode == 20, (args, result)
    assert not result.stdout, (args, result)
    assert 'Full-flash backup storage is required' in result.stderr
    assert 'no card access was attempted' in result.stderr
for arg in ('--help', '--version'):
    result = subprocess.run([binary, arg], capture_output=True, text=True)
    assert result.returncode == 0 and not result.stderr, (arg, result)
print('Mandatory-backup gate: six active forms refused before backend access; help/version passed')
