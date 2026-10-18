#!/usr/bin/env python3
"""Synthetic candidate audit faults. No private firmware or identifiers.
SPDX-License-Identifier: GPL-2.0-only
"""
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from audit_nv_capture import audit, pdrs

# Original fixture CRC via polynomial long division, independent of the tool's
# reflected bit-at-a-time implementation.
def reverse(value, bits):
    return int(f'{value:0{bits}b}'[::-1], 2)


def crc(data):
    value = 0
    for byte in data:
        value ^= reverse(byte, 8) << 8
        for _ in range(8):
            value = ((value << 1) ^ (0x8005 if value & 0x8000 else 0)) & 0xffff
    return reverse(value, 16)


def make_capture(nic=0x800c, config=3, extra=b''):
    pda = (struct.pack('<6H', 5, 8, nic, 0, 1, 0) +
           struct.pack('<3H', 2, 0x400, config) + extra + struct.pack('<2H', 2, 0))
    pda += struct.pack('<H', crc(pda))
    window = bytearray((i * 37 + i // 256) & 255 for i in range(0x20000))
    window[0x150c:0x1518] = b'PK010007.HEX'
    window[0xca08:0xca14] = b'SF010306.HEX'
    window[0xfe00:0xfe07] = bytes.fromhex('20040b000073ff')
    window[0x10000:0x10400] = pda.ljust(1024, b'\xff')
    return window * 64


good = make_capture()
report = audit(good, good)
assert report['candidate_observations_match'] and report['pda_crc_valid']
assert not report['hardware_authorization'] and not report['physical_flash_coverage_verified']
assert not report['fresh_operation_backup'] and not report['sram_capacity_verified']

bad_alias = bytearray(good)
bad_alias[0] ^= 1
bad_crc = bytearray(good)
for offset in range(0x10000, len(bad_crc), 0x20000):
    bad_crc[offset + 5] ^= 1
for first, second in ((good[:-1], good), (good, bad_alias),
                      (bad_alias, bad_alias), (bad_crc, bad_crc),
                      (bytes(len(good)), bytes(len(good))),
                      *((x, x) for x in (make_capture(nic=0x800d),
                                         make_capture(config=1),
                                         make_capture(extra=struct.pack('<3H', 2, 5, 1)),
                                         make_capture(extra=struct.pack('<3H', 2, 0x400, 3))))):
    try:
        audit(first, second)
    except ValueError:
        continue
    raise AssertionError('Candidate audit accepted malformed/incompatible capture')
print('Candidate audit: exact size/pair/alias/PDA CRC/NIC/configuration/duplicate checks passed; no hardware authorization')

# Every alias carries the same wrong/truncated/duplicate CIS; aliasing alone
# must not satisfy candidate identity.
for chain in (bytes.fromhex('20040c000073ff'), bytes.fromhex('20ff'),
              bytes.fromhex('20040b00007320040b000073ff'), b'\xff'):
    window = bytearray(good[:0x20000])
    window[0xfe00:0x10000] = b'\x00' * 512
    window[0xfe00:0xfe00 + len(chain)] = chain
    bad = window * 64
    try:
        audit(bad, bad)
    except ValueError:
        continue
    raise AssertionError('Accepted invalid CIS identity/chain')
# Exact byte-length boundary, including valid-CRC oversized records.
for words in (256, 257):
    data = struct.pack('<HH', words, 0x1234) + bytes((words - 1) * 2)
    data += struct.pack('<HH', 2, 0)
    data += struct.pack('<H', crc(data))
    try:
        pdrs(data)
    except ValueError:
        assert words == 257
    else:
        assert words == 256

window = bytearray(good[:0x20000])
window[0x150c] ^= 1
try:
    audit(window * 64, window * 64)
except ValueError:
    pass
else:
    raise AssertionError('Accepted wrong firmware anchor')
