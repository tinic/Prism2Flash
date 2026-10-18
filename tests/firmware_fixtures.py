# SPDX-License-Identifier: GPL-2.0-only
"""Original synthetic records, never proprietary firmware or real calibration."""
import struct


def srec(kind, address, data=b''):
    width = {0: 2, 3: 4, 5: 2, 6: 3, 7: 4}[kind]
    raw = bytes([width + len(data) + 1]) + address.to_bytes(width, 'big') + data
    return f'S{kind}' + (raw + bytes([(~sum(raw)) & 255])).hex().upper() + '\n'


def words(*values):
    return struct.pack('<' + 'H' * len(values), *values)


def info(kind, data, fragment=None):
    count = len(data) // 2 + (0 if kind == 0x8001 else 1)
    raw = words(count, kind) + data
    if fragment:
        return srec(3, 0xff200000, raw[:fragment]) + srec(3, 0xff200000 + fragment, raw[fragment:])
    return srec(3, 0xff200000, raw)


def firmware(entry=0, component=0x1f, nic=0x8013, nic_variant=1, include_name=True,
             fragmented=False, extra='', actor_variant=2, address=None):
    station = component == 0x1f
    address = (0x7e1800 if station else 0x7e0000) if address is None else address
    text = srec(0, 0, b'SYNTHETIC - NOT EXECUTABLE FIRMWARE')
    # Out-of-order adjacent pieces exercise sorting and byte-exact coalescing.
    text += srec(3, address + 32, bytes(range(32, 64)))
    text += srec(3, address, bytes(range(32)))
    text += info(1, words(component, 4, 1, 7), 5 if fragmented else None)
    text += info(4, words(nic, nic_variant, 1, 0))
    text += info(2, words(1, 1, 1, 1, 3))
    text += info(2, words(1, 2, 1, 1, 3))
    if station:
        text += info(2, words(1, 3, actor_variant, 3, 6))
        text += info(2, words(0, 4, 1, 1, 4))
    else:
        text += info(2, words(0, 3, 2, 3, 6))
    text += info(3, words(123))
    if include_name:
        text += info(0x8001, b'SYNTHETIC.HEX\0')
    text += srec(3, 0xff000000, struct.pack('<III', 3, address + 16, 4))
    if station:
        text += srec(3, 0xff000000, struct.pack('<III', 0x101, address + 4, 6))
        text += srec(3, 0xff000000, struct.pack('<III', 0xffffffff, address + 32, 16))
        text += srec(3, address + 0x100, bytes(range(16)))
        text += srec(3, 0xff100000, struct.pack('<III', address, 0x110, 1))
    text += extra
    return text + srec(7, entry)


def crc16(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xa001 if crc & 1 else 0)
    return crc


def pda(nic=0x8013, variant=1, missing=None, duplicate=False, mac=b'\x02\x00\x00\x00\x00\x01'):
    records = [(8, words(nic, variant, 1, 0)),
               (6, words(0, 1, 1, 1, 3)), (7, words(0, 2, 1, 1, 3)),
               (3, b'TEST'), (0x101, mac)]
    if duplicate:
        records.append(records[0])
    data = b''.join(words(len(value) // 2 + 1, kind) + value
                    for kind, value in records if kind != missing)
    data += words(2, 0)
    data += words(crc16(data))
    assert crc16(data) == 0
    return data.ljust(1024, b'\0')


CARD = 'P2CARD 1\nNIC 0x8013 1 1 0\nMAC 02:00:00:00:00:01\nSUP 3 2 3 6\nSUP 4 1 1 4\n'


def write_fixtures(directory):
    files = {'station.hex': firmware(), 'ram.hex': firmware(entry=0x3f0c01),
             'primary.hex': firmware(component=0x15), 'card.txt': CARD,
             'pda.bin': pda(), 'bad.hex': firmware().replace('S325', 'S324', 1)}
    for name, data in files.items():
        (directory / name).write_bytes(data.encode() if isinstance(data, str) else data)
