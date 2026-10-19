# Offline firmware inspection, validation and preparation

`Prism2Firmware` is a native 68000 and host utility. It links the portable
image/PDA code only; it has no card backend. All commands read files and print
results. No command opens card.resource or accesses PCMCIA. Preparation changes
private in-memory copies only; source/PDA files are unchanged and no output image
is written.

## Commands

```text
Prism2Firmware --version
Prism2Firmware inspect image.hex
Prism2Firmware inspect-initial ID010001.HEX
Prism2Firmware validate station.hex --mode flash --card card.txt --pda pda.bin
Prism2Firmware validate station.hex --mode flash --card card.txt --pda pda.bin --primary primary.hex
Prism2Firmware prepare station.hex --mode flash --card card.txt --pda pda.bin --primary primary.hex
```

Use `--mode ram` for RAM/Genesis image compatibility checks. An image with a
zero S7 entry is flash class; a nonzero entry is RAM class. These are validation
modes and never request a card operation. There is no force, flash or download
command. Return code 0 means the requested checks passed; rejected input or
file errors return 20.

`inspect-initial` is a separate raw RAM-bootstrap reader for DOS initial images.
It requires S7=0, bounded even 23-bit RAM regions, checked record counts,
checksums and nonoverlap. It does not classify that marker as flash, apply PDA
plugs or stamp signatures. It reports every region and SHA-256; no loading or
card compatibility is implied. Regular `inspect` continues to require component
metadata and rejects raw initial images.

Inspection reports component identity, entry point, payload segments, exact
platform identities, supplier/actor ranges, PDA plugs and signature descriptors.
It checks structure and checksums without claiming compatibility with a card.
Unknown metadata types and S-record dialects are refused instead of ignored.

## Saved card snapshot

The text format is deliberately small. Example using **synthetic values**:

```text
P2CARD 1
NIC 0x8013 1 1 0
MAC 02:00:00:00:00:01
SUP 3 2 3 6
SUP 4 1 1 4
```

`NIC` values are **id, variant, major, minor**. `MAC` is six colon-separated hex
bytes; it must be nonzero unicast. `SUP` values are **interface, variant,
bottom, top**, with supplier role implied. Interface numbers are MF=1, CF=2,
PRI=3 and STA=4. Integers are decimal or `0x` hexadecimal. Blank lines and
whole-line `#` comments are permitted. Duplicate fields, unknown fields,
trailing tokens, invalid numbers and malformed ranges are rejected.

NIC and MAC are required. MF/CF suppliers are obtained from PDA records
0x0006/0x0007; optional `SUP 1`/`SUP 2` lines must match them exactly. Other
suppliers must be recorded from the card's corresponding RIDs when required
by image actor records. With `--primary`, its unambiguous PRI supplier replaces
the saved PRI supplier for station compatibility checking. The new primary
itself is checked against the saved card data first.

Use a prior, trustworthy capture from the same card. The current Genesis
utility does not yet export snapshots or PDA backups. Synthetic examples do
not describe a supported card. Text snapshots are editable input and cannot
prove their own provenance; NIC and MAC matching prevents accidental cross-card
PDA selection but does not authenticate the capture or certify a memory map.

## PDA requirements

`pda.bin` must contain exactly 1024 bytes, with bounded little-endian PDRs, a
correct end record and a valid reflected CRC-16/ARC (reflected polynomial 0xa001, initial value 0). Duplicate PDRs are
rejected. PDR 0x0008 must match the saved NIC's four identity fields; PDR 0x0101
must match the saved MAC. MF/CF supplier records must have the expected roles,
interface ids and lengths.

Every image plug must point to a fully covered, contiguous payload range.
Plug destinations cannot overlap. Each ordinary PDR must exist in the backup
with the exact requested length. No default calibration or short-PDR extension
is accepted. The special 0xffffffff filename field needs only a bounded
destination. Preparation zero-fills it and copies the actual source basename,
truncated to length minus one, with a terminating NUL. Embedded name metadata
is not the source filename. Both Unix and Amiga path separators are handled.

PDA checksums establish structural integrity; they do not authenticate a backup
or establish that RF calibration values are physically correct for a card.

`validate` checks requirements without applying patches. `prepare` first applies
the same checks, then builds a private prepared value as described below.

## Validation and bounds

- Require a platform NIC ID match, following DOS ILHOPFW. Marketing names,
  image filenames and historical RAM-mode compatibility exceptions do not
  override it.
- Require matching actor/supplier variants and overlapping version intervals,
  conservatively per record as Host AP does. A replacement PRI pair does not
  bypass variant checks. Missing or ambiguous required data fails.
- Accept PRI 0x15 and STA 0x1f for validation. Other components can be inspected
  when structurally understood but are not approved by validation.
- Check RAM/flash class, even payload alignment and the current 23-bit AUX
  transport ceiling. These broad transport bounds are not a card flash map.
- Check signature flags, bounds, endpoint coverage and stamp/plug conflicts.
  Programmed regions must start at a payload segment. Gaps inside a descriptor
  remain represented as gaps; no bytes or calculated CRC are invented.
- Optional PRI/STA pair checks require both images in the selected mode and
  reject overlapping payload regions. This does not merge them or resolve the
  later signature-stamp overlap at a primary/station boundary.

## Preparation

`prepare` works entirely offline and reports exact raw source/PDA SHA-256,
prepared region addresses, lengths, hashes and the separate execution entry.
The source digest is collected from the same input stream used by the parser,
including original line endings. It is not calculated by reopening the path.
The report does not export a payload or authorize card access.

The `PreparedImage::create` C++ factory validates all sources before copying
them, applies exact-length PDA plugs, fills source filename fields, and prepends
`DE C0` at each programmed descriptor's `start - 2`. This is Host AP's fixed
signature stamp, not a calculated image CRC. Duplicate stamps and resulting
overlaps fail. Descriptors with program=0 do not generate a stamp. Gaps remain
gaps; only adjacent bytes are coalesced.

With `--primary`, it merges the prepared PRI and STA copies transactionally.
The only overlap exception follows Host AP: a two-byte STA stamp may replace
the final two PRI bytes when PRI ends exactly at the original STA region start.
This exception is refused if those bytes contain a PRI plug or signature.
All other overlap is rejected. Combined RAM images retain the PRI entry point.
Primary-only flash preparation is prohibited; standalone RAM PRI preparation
remains available for future Genesis bootstrap work.

Preparation exposes immutable segments of a move-only owning value. On any
error the private copies are destroyed; input images and PDA are unchanged.
Both inputs retain their individual parser limits, so a pair can use at most
2 MiB payload plus 256 signature bytes and 16,384 prepared segments before
coalescing. Out-of-memory errors fail explicitly. No implicit flash geometry,
SRAM width or physical-card approval is inferred from successful preparation.

The parser permits optional S0 and S5/S6 count records, S3 data/metadata and one
S7 entry record anywhere in the Intersil stream. It rejects invalid hex/counts/
checksums, incomplete fragments, duplicates, payload overlap and address wrap.
It explicitly decodes metadata bytes without unaligned native-word loads.

Limits per image: 8 MiB input, 65,536 lines, 1 MiB payload, 8,192 input payload
segments, 256 platforms/ranges/plugs each, 64 signature descriptors, 4 KiB per
information record and 127 name bytes. Limits are ceilings, not minimum memory
requirements; allocation failures return an error. Firmware state and payloads
are on the heap. Native smoke tests use a 4096-byte Shell stack.

## Tests and reproducibility

```sh
tools/ci.sh host
CXX=clang++ tools/ci.sh sanitize
tools/ci.sh native
```

Host CI uses original synthetic records and PDA data. It covers successful
inspection/validation, entry/load-address distinction, fragmented metadata,
NIC/MAC binding, modes, interface ranges, missing calibration, signature bounds,
malformed lines, input limits and 120 deterministic mutated inputs. A C++ API
check verifies exact payload bytes, sorting/coalescing and failure ownership.
Preparation tests compare every resulting byte/address/entry with the retained
Host AP parser/plug/stamp/merge routines on original, valid synthetic data. The
legacy adapter excludes Linux device access and never processes malformed input.
It omits synthetic S0 headers, which the legacy parser does not support.
Separate tests cover malformed input, conflicts and preservation of originals.

With separately obtained, checksum-matching private images, compare parsed
metadata and payload bytes against the retained upstream parser:

```sh
python3 tests/compare_upstream.py --images /path/to/private/images \
  --output build-host/upstream-comparison.json
```

No firmware is bundled, downloaded by CI, or needed for normal tests. See
[FIRMWARE-FINDINGS.md](FIRMWARE-FINDINGS.md) for measured image hashes and
provenance. Use `tests/emulator_smoke.py --suite firmware` with Amiberry or
Copperline and a locally supplied ROM for native file-processing smoke tests.

Prepared-payload export, binary/text backup export, card-profile address maps,
Genesis RAM bootstrap and permanent flashing remain separate milestones.
Hardware validation remains blocked by the mandatory full-backup gate.
