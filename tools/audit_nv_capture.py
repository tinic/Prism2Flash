#!/usr/bin/env python3
"""Read-only audit of two existing AUX captures against the MA401 candidate.

This cannot approve physical coverage, establish independent acquisitions or
authorize hardware operations. No card interface is linked or invoked. Outputs
omit MAC, serial, calibration bytes and input paths.
SPDX-License-Identifier: GPL-2.0-only
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

NV_BYTES = 0x800000
WINDOW_BYTES = 0x20000
PDA_ADDRESS = 0x7f0000
PDR_LENGTH_MAX = 512  # Length field in bytes, excluding its own word.
CIS_OFFSET = 0xfe00


def crc_arc(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xa001 if crc & 1 else 0)
    return crc


def pdrs(data):
    records = {}
    pos = 0
    while pos + 4 <= len(data):
        words, kind = struct.unpack_from('<HH', data, pos)
        end = pos + 2 * (words + 1)
        if not words or words * 2 > PDR_LENGTH_MAX or end > len(data):
            raise ValueError('Malformed PDA record bounds')
        if kind == 0:
            if words != 2 or crc_arc(data[:end]):
                raise ValueError('Invalid PDA end record or CRC-16/ARC')
            return records
        if kind in records:
            raise ValueError('Duplicate PDA record')
        records[kind] = data[pos + 4:end]
        pos = end
    raise ValueError('Missing PDA end record')


def cis_identity(window):
    """Walk the candidate's bounded CIS chain; never search for a byte pattern."""
    pos = CIS_OFFSET
    identity = None
    while pos < 0x10000:
        kind = window[pos]
        pos += 1
        if kind == 0xff:
            if identity is None:
                raise ValueError('Missing candidate CIS MANFID')
            return identity
        if kind == 0:
            continue
        if pos >= 0x10000:
            break
        length = window[pos]
        pos += 1
        end = pos + length
        if end > 0x10000:
            break
        if kind == 0x20:
            if length != 4 or identity is not None:
                raise ValueError('Invalid or duplicate CIS MANFID')
            identity = struct.unpack_from('<HH', window, pos)
        pos = end
    raise ValueError('Malformed candidate CIS chain')


def audit(first, second):
    if len(first) != NV_BYTES or len(second) != NV_BYTES:
        raise ValueError('Both captures must be exactly 8388608 bytes')
    if first != second:
        raise ValueError('Raw captures differ; no repair or voting is permitted')
    window = first[-WINDOW_BYTES:]
    if len(set(window)) < 2:
        raise ValueError('Constant flash window is not candidate evidence')
    if any(first[offset:offset + WINDOW_BYTES] != window
           for offset in range(0, NV_BYTES, WINDOW_BYTES)):
        raise ValueError('The 64 observed 128 KiB windows are not identical')
    if cis_identity(window) != (0x000b, 0x7300):
        raise ValueError('CIS tuple differs from the exact candidate')
    for offset, name in ((0x150c, b'PK010007.HEX'), (0xca08, b'SF010306.HEX')):
        if window[offset:offset + len(name)] != name:
            raise ValueError('Firmware anchor differs from candidate observation')
    records = pdrs(first[PDA_ADDRESS:PDA_ADDRESS + 1024])
    nic = struct.pack('<4H', 0x800c, 0, 1, 0)
    if records.get(8) != nic:
        raise ValueError('PDA NIC tuple differs from the exact candidate')
    if records.get(0x400) != b'\x03\x00':
        raise ValueError('PDA configuration differs from candidate PDR0400=0003')
    if 5 in records:
        raise ValueError('PDR0005 presence differs from this candidate evidence')
    return {
        'format': 'Prism2 offline candidate audit 1',
        'profile_id': 'ma401-pd5lmwp200-candidate-1',
        'candidate_observations_match': True,
        'hardware_authorization': False,
        'physical_flash_coverage_verified': False,
        'fresh_operation_backup': False,
        'raw_sha256': hashlib.sha256(first).hexdigest(),
        'candidate_window_sha256': hashlib.sha256(window).hexdigest(),
        'captured_bytes': NV_BYTES,
        'identical_windows': 64,
        'window_bytes': WINDOW_BYTES,
        'pda_crc_valid': True,
        'firmware_anchors_match': True,
        'cis': {'manufacturer': 0x000b, 'product': 0x7300},
        'nic': {'id': 0x800c, 'variant': 0, 'major': 1, 'minor': 0},
        'pdr_0400': 3,
        'pdr_0005_present': False,
        'sram_width_inference': 16,
        'sram_capacity_verified': False,
        'limitations': ['Saved files cannot authorize a new operation',
                        'Aliasing, firmware anchors and filing photos do not prove physical decode',
                        'No physical card or internal part identity read'],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('first', type=Path)
    parser.add_argument('second', type=Path)
    args = parser.parse_args()
    try:
        if args.first.samefile(args.second):
            raise ValueError('Supply two separate capture files')
        for path in (args.first, args.second):
            if path.stat().st_size != NV_BYTES:
                raise ValueError('Both captures must be exactly 8388608 bytes')
        def read_capture(path):
            with path.open('rb') as stream:
                return stream.read(NV_BYTES + 1)
        result = audit(read_capture(args.first), read_capture(args.second))
    except (OSError, ValueError) as error:
        # Avoid exposing private filenames from OSError's default formatting.
        reason = str(error) if isinstance(error, ValueError) else 'Cannot read capture files'
        print(json.dumps({'candidate_observations_match': False,
                          'hardware_authorization': False, 'error': reason}))
        return 20
    print(json.dumps(result, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
