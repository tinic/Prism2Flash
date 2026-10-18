# Firmware findings incorporated — 2026-10-18

Flash analyzed the 1.7.4 mirror bundle and prism2v2 2.9 without touching a
card. Its corrected report was read from the development host playhouse2.
The conclusions below were checked against the retained Host AP utilities
0.4.7 parser. These are software interoperability findings, not tested card
profiles or permission to program a device.

## Primary sources and provenance

- Host AP utilities 0.4.7 `prism2_srec.c` and `util.c`, retained unmodified in
  `third_party/hostap/hostap-utils-0.4.7.tar.gz` under GPL-2.0.
- [prism2v2 2.9](https://aminet.net/package/driver/net/prism2v2): Manual,
  “Firmware upgrade”, and `Source/unit.c` / `Source/request.c`.
- [Historical firmware mirror](https://junsun.net/old/linux/intersil-prism/firmware/1.7.4/):
  private local copies used for comparison only. No proprietary image is
  committed, distributed or fetched by CI. Checksums and measured metadata
  are recorded in [firmware-images.json](evidence/firmware-images.json).

The Manual recommends PRI 1.1.1 and STA 1.7.4 because prism2.device was tested
with them, with alternatives for cards lacking those versions. This is not a
universal chipset requirement or a filename-based selection rule.

## Measured variants

| Image | Component | Metadata version (major.minor.variant) | Class | S7 entry |
| --- | --- | --- | --- | --- |
| pk010101.hex | PRI 0x15 | 1.1.1 | flash | 0 |
| pm010102.hex | PRI 0x15 | 1.1.2 | RAM | 0x003f0000 |
| af010104.hex | PRI 0x15 | 1.1.4 | RAM | 0x003f0000 |
| ak010104.hex | PRI 0x15 | 1.1.4 | RAM | 0x003f0000 |
| sf010704.hex | STA 0x1f | 1.7.4 | flash | 0 |
| rf010704.hex | STA 0x1f | 1.7.4 | RAM | 0x003f0c01 |
| ru010704.hex | STA 0x1f | 1.7.4 | RAM | 0x003f1801 |

`af` and `ak` are RAM primary images. The analyzed set contains no Host AP
component 0x14b. Each image carries its own platform identities and interface
ranges. Only `pk` and `sf` in this set have flash class; their pairing still
needs the exact card and interface checks. RAM variants have different platform
sets and payload bytes. A name or advertised version cannot establish suitability.

Host AP rejects zero-entry images in RAM/Genesis mode and nonzero-entry images
in flash mode (`prism2_srec.c:1473`). S7 supplies the execution/start address;
S3 supplies independent per-region load addresses (`:1536`, `:1546`). A nonzero
entry is not a universal Genesis load address. Real Intersil RAM files often
place S7 before their payload records, so this dialect requires a unique S7
without treating its position as an end-of-file constraint.

## PDA and signature handling

Special S3 records use explicit little-endian fields:

- `0xff000000`: PDR id, plug destination and byte length, each u32.
- `0xff100000`: signature-region start, length and program flag, each u32.
- `0xff200000` and continuation offsets: word-count/type metadata records,
  including component, platform, interface, sequence and name data.

The station image contains calibration, regulatory and MAC-address plug
requirements. Preserve each card's PDA and match every required field before
preparation. This validator requires exact plug lengths and refuses missing
records; it does not copy Host AP's permissive default-data fallback.

**Host AP's `generate_crc16` does not calculate an image CRC.** Its source
explicitly says CRC support is unimplemented and prepends bytes `DE C0` at
`start - 2` for programmed signature records (`prism2_srec.c:1378`). The region
length may span gaps. A valid descriptor is not evidence of payload integrity.
The new tool checks descriptor bounds but does not stamp or modify the image.
PDA CRC validation is a separate, real checksum check over the PDA end record.

## Driver capability and remaining limits

In prism2v2 `unit.c:501–505`, the Intersil path sets TKIP/CCMP flags when the
station identity reports version 1 and revision at least 7. `request.c:1848–1854`
exports encryption types from those flags. The crypto implementation is host
code. Firmware-version capability reporting does not prove association or
encrypted traffic; those remain later hardware acceptance tests. Hermes RAM
files belong to a separate firmware family.

The card's memory layout, SRAM configuration, safe programming map and actual
Genesis startup still require card-specific evidence. NICID 800c ambiguity,
SSF, damaged-CIS recovery and early HFA3841 remain open. Reverse engineering
the firmware ISA is unnecessary for parsing images and using the host interface.
Community advice to avoid particular firmware versions is not treated as a
validated restriction without primary evidence.

## Comparison evidence

The new parser's component, entry, platforms, interface ranges, plugs,
signature descriptors, coalesced payload addresses and every payload byte
match Host AP's parser for all seven checksum-pinned private images. See
[upstream-parser-comparison.json](evidence/upstream-parser-comparison.json).
The comparison harness excludes device routines and accepts only pinned valid
images. The legacy parser is not used as an oracle for malformed input.
This comparison covers parsed images; prepared-image output is not implemented.
