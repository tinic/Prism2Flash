# Roadmap

## 1. Establish Genesis access

- [x] Separate Git repository and GPL source/license setup.
- [x] Portable protocol layer and deterministic host model.
- [x] Native card.resource ownership and removal handling.
- [x] pccard.library CIS decoding and mapped I/O access.
- [x] Deadline-based commands, AUX transfers and validated PDA reads.
- [x] Explicit x8/x16 Genesis RAM-access probe and normal-mode return.
- [x] Host tests and native 68000 cross build.
- [ ] A600/A1200 validation of CIS layout, register/payload byte order, timing,
      entry/readback/exit, cancellation, and firmware restart.

Implementation is experimental until the hardware gate passes. A successful
four-byte RAM probe does not establish that bootstrap firmware will execute.

## 2. Genesis RAM bootstrap

Port the image-processing core of prism2_srec behind a transport-independent
API. Keep S-record parsing, component/interface checks, PDA plugging, primary/
station merging and signature stamping. Audit old parser errors and all
native-word/unaligned accesses rather than merely recompiling the Linux code.

Add binary/text PDA and CIS backups, offline prepare/inspect commands, and
comparison fixtures against upstream prepared images. Backups of PDA/CIS are
not complete firmware backups.

Define exact card profiles: CIS IDs, NIC component/variant, SRAM configuration,
memory maps, eligible bootstrap images and allowed RAM ranges. The initial
probe's working-firmware identity gate must be replaced with a separately
validated bootstrap path for cards that cannot answer RIDs. Never silently
skip the gate or infer the profile from a generic marketing name.

Load a supported primary/bootstrap image into RAM through Genesis, verify
every transferred region, start it, and confirm primary-firmware RIDs. Keep
ownership and power for the recovery session: returning from a short-lived
CLI command through normal cleanup would discard the RAM bootstrap state.

Special cases: NICID 800c's different layouts; early HFA3841; damaged CIS;
multifunction/ConnectPlus; SSF. Add them only with separate evidence and tests.

## 3. Complete the Genesis recovery tool

Implement a shared flash engine that can run from the temporary primary
firmware. Read download-buffer RID FD01, validate its capacity, enable AUX,
stage bounded blocks and issue download/program commands with long deadlines.

Prepare the entire update in RAM before erasing anything. Validate image
addresses, compatibility, PDA patching and companion primary/station images.
Preserve CIS/PDA storage and implement readback against the correct physical
flash map. Confirm restart and identities. Log the last completed phase/block.
After a programming failure, preserve the recovery session where possible;
do not blindly retry/reset or release a card still dependent on RAM firmware.

Hardware gate: recover a spare card using a known matching image and a proven
independent recovery route. Power-interruption experiments require a separate,
explicit test decision and expendable hardware.

## 4. Regular firmware flasher

Build Prism2Flash around the same validated engine, using the card's existing
primary firmware instead of a Genesis bootstrap. Provide offline image
inspection, compatibility check, backups, explicit update selection, progress
and verification. Keep primary-only regular updates prohibited.

Expand the tested hardware matrix. Package Amiga binaries, documentation,
source, dependency notices and checksums. AmiNetXDuo integration comes after
the standalone tools are proven.
