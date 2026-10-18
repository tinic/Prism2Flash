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
Validation checks descriptor bounds without modifying the image. The offline
prepare factory now stamps private copies; source files remain unchanged.
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
This private-image comparison covers parsing only. The separate preparation
tests compare every prepared byte against the retained upstream plug/stamp/merge
routines on synthetic fixtures. No proprietary prepared image is redistributed.

## MA401 revision evidence from the supplied label

The user's 2026-10-18 underside photo reads **FCC ID PY3MA401RD**, **5 Volt**,
and **16-bit PCMCIA**. The FCC filing identifies model MA401RD, granted
2003-02-09. This is stronger physical-revision evidence than the generic
MA401RA/ISL37300P/Eval-RevA CIS strings observed on the inserted card. The
PCMCIA bus width on the label does not identify the internal SRAM width.
The user's MAC/serial and photograph are not included in the public repository.

The filing lists internal-photo exhibits 301850 (two pages) and 301854 (five
pages). Automated PDF retrieval initially failed; the user later supplied
Desktop copies, inspected below. Schematics/block diagram/technical description are
listed as confidential metadata. Sources:
[FCC filing](https://fccid.io/PY3MA401RD),
[internal photos 301850](https://fccid.io/PY3MA401RD/Internal-Photos/Internal-Photos-301850),
[SAR photos 301854](https://fccid.io/PY3MA401RD/Internal-Photos/EUT-Photo-for-SAR-report-301854).

The Intersil ISL37300P reference-evaluation data sheet describes a 3.3 V board;
its electrical assumptions must not be substituted for this retail card's 5 V
label ([manufacturer data sheet transcription](https://dtsheet.com/doc/160214/intersil-isl37300p-cd)).
Neither the FCC ID alone nor 128 KiB AUX aliasing closes the full physical
flash coverage requirement. The native allowlist/Genesis gate stays unchanged.
See [the next hardware/flash plan](WRITE-AND-FLASH-PLAN.md).

Claude's relayed suggestion of one 128K x8 SRAM plus SST39VF010-type flash is a
research lead, not an established profile. The reproduced Intersil platform
table explicitly lists both **x8 and x16 RAM** for the 800C/8013/8017/801B group
under SST NVRAM ([platform table](https://junsun.net/linux/intersil-prism/IDtable.html)).
That table reports a platform/release family, not a JEDEC chip identification
read or an exact installed-memory part number. Its x8/x16 alternatives prevent
using this NIC ID as proof that every MA401 revision has one shared arrangement.
Exact SRAM vendor is not required by the software; proven width, capacity,
decode and physical nonvolatile coverage are required. No proposed replacement
part or footprint is approved by this project.

### Follow-up through playhouse2: primary data sheet and PDA configuration

The FCC exhibits still return HTTP 403 from playhouse2. An alternate archive
did provide Intersil **FN8019.2, June 2002**, the 42-page ISL3873B data sheet
([archive entry](https://www.datasheetarchive.com/datasheet/ISL3873B/Intersil?term=ISL3873B&version=1)).
The retrieved PDF SHA-256 is
`8d9bf148b7f1b9e4b18b4b8c5d7bcc4fb0b7b88e0ad6aa43e1950dcecdeb27b3`.
Pages 11–12 explicitly show multiple configurations, including two x8 SRAMs
or a single x16 SRAM. The text permits different flash/RAM organizations and
describes 256 KiB organized 128K x16 as typical for station designs. These
examples disprove a universal one-chip 128K x8 arrangement; they do not identify
the memory actually fitted to this card.

Offline inspection of both previously verified AUX NV captures found the same
CRC-valid PDA at AUX `0x7f0000`, stored CRC `0x82a2`. Its NIC configuration PDR
`0x0400` has bytes `03 00`, value `0x0003`. The retained Host AP 0.4.7
`plug_pdr_0400` constructs bit 1 from `ram16`, bit 2 from PCI and sets bit 0
(`prism2_srec.c:1277`). **Inference: this card's saved PDA configuration points
to x16 SRAM and PCMCIA.** This is stronger evidence than a generic part list,
but still needs independent profile corroboration. No SRAM-size PDR `0x0005`
was present. Neither SRAM vendor/capacity nor flash width/capacity/coverage
follows from this field. See the non-private summary in
[pda-memory-configuration.json](evidence/pda-memory-configuration.json).

This analysis reads only existing files. It does not change the native allowlist,
select SRAM automatically, satisfy the fresh full-backup gate or perform a new
card operation.

### User-supplied FCC photo PDFs

The user subsequently supplied two PDFium-saved Desktop PDFs. Both were copied
unchanged into private project evidence and every page was visually inspected.
The public repository retains observations and hashes only, not the PDFs or
the user's label photograph.

| Exhibit | Pages | Supplied PDF SHA-256 |
| --- | --- | --- |
| Internal photos | 2 | `009f3c745f38eca7813f7b4b2173cbfcac025221b20f8384d8cb6353432689f7` |
| SAR/EUT photos | 5 | `3e12646fb8172eaa788338376a98123a96508adc81c6a13f85fcb2e37f6ce663` |

The two-page exhibit labels the pictured boards **MA401 rev.D**. The SAR
photographs identify the EUT as **DNI LM-WP200RB**, report 920109R03A. These
are filing samples; do not silently treat their chip markings as direct
inspection of the user's particular unit.

In SAR page 3's underside photograph, **U5** bears the SST MPF logo and its
part marking appears to read **39VF010**, with `70-4C-WH` below. The speed/package
suffix is less certain than the family identification. Microchip identifies
SST39VF010 as **1 Mbit, 128K x8 (128 KiB)** parallel flash, with uniform 4 KiB
erase sectors ([product](https://www.microchip.com/en-us/product/sst39vf010),
[manufacturer data sheet](https://ww1.microchip.com/downloads/aemDocuments/documents/MPD/ProductDocuments/DataSheets/SST39LF010-SST39LF020-SST39LF040-SST39VF010-SST39VF020-SST39VF040-Data-Sheet-DS20005023.pdf)).
This corroborates a candidate 128 KiB flash profile and the observed alias period.
It does not prove the exact fitted part or complete physical coverage of this
unit without independent profile/address-map review.

The front-side U1 device's marking remains unreadable in these images. A large
controller is visible, but its exact suffix is not confidently legible either.
Do not infer an SRAM vendor, capacity or replacement part from the package
outline. The saved PDA's x16 configuration clue remains the stronger evidence
for selecting a future, independently corroborated Genesis profile. No hardware
gate is enabled by this photographic analysis.
