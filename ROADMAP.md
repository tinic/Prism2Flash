# Roadmap

## Required gate: timestamped full-flash readback

- [x] Refuse native active inspection/probing before slot access while full
      backup support is unavailable; retain the host simulation for development.
- [ ] Independently review AUX NV addressing and exact physical flash maps.
- [x] Implement experimental non-resetting AUX address-space acquisition, repeated
      reads, timestamped raw files, SHA-256 manifests and saved-file verification.
- [x] Verify two complete repeated AUX NV captures and saved files on MA401RA,
      with bounded consecutive page agreement and retained failure evidence.
- [ ] Independently establish complete physical flash coverage/profile. The
      verified AUX capture does not yet authorize active operations.
- [ ] Require a fresh verified full backup before every impactful operation and
      timestamped full readback afterward; reject partial/failed/stale backups.
- [ ] Verify failure/removal/storage handling and operation gating offline, then
      validate the acquisition on spare hardware when explicitly made available.

This gate precedes any active hardware inspection, initialization, reset,
Genesis probe, bootstrap, flashing or restore. No skip/force path. See
[the backup contract](docs/HARDWARE-TESTING.md#mandatory-full-flash-backups).
PDA/CIS exports alone never satisfy it. Offline parser work can continue.

## 1. Establish Genesis access

- [x] Separate Git repository and GPL source/license setup.
- [x] Portable protocol layer and deterministic host model.
- [x] Native card.resource ownership and removal handling.
- [x] pccard.library CIS decoding and mapped I/O access.
- [x] Deadline-based commands, AUX transfers and validated PDA reads.
- [x] Explicit x8/x16 Genesis RAM-access probe and normal-mode return.
- [x] Bounded write-once RAM chunks, consecutive exact readback and preserved
      AUX state; model fault coverage includes cancellation/removal during cleanup.
- [x] Host tests and native 68000 cross build.
- [x] Native startup, argument handling and empty-slot exits in Amiberry and
      Copperline on A600/68000 and A1200/68020 profiles.
- [x] Real A1200 startup/offline checks and repeated CIS-only identification
      (`000b:7300`), with published-binary hashes and backup refusal checks.
- [ ] Verified MA401RA configuration/physical-flash/SRAM profile; its CIS ID is
      supported by Linux Host AP but absent from the current narrow allowlist.
- [ ] A600/A1200 validation of CIS layout, register/payload byte order, timing,
      entry/readback/exit, cancellation, and firmware restart.

The protocol implementation is experimental; native active commands are
currently blocked by the mandatory-backup gate above. A successful
four-byte RAM probe does not establish that bootstrap firmware will execute.

## 2. Offline images and Genesis RAM bootstrap

- [x] Bounded, byte-oriented S-record and Prism metadata parser.
- [x] Offline inspection and mode/platform/interface/PDA requirement validation.
- [x] Saved card NIC/MAC binding and strict missing-calibration rejection.
- [x] Replacement PRI/STA metadata/payload-overlap validation.
- [x] Synthetic malformed-input tests and valid-image comparison with Host AP.
- [x] Apply PDA plugs and special filename records to a private prepared copy.
- [x] Implement signature stamping and transactional merge with synthetic upstream comparisons.
- [ ] Export prepared payload packages with checked closes/reopen verification
      and input/card/PDA provenance; the current prepare command reports hashes only.
- [ ] Export binary/text PDA, CIS and card snapshots when hardware is available.
- [ ] Validate card-specific memory maps before enabling any image loading.


The parser/validator and private-copy preparation are implemented behind a
transport-independent API; see include/firmware.h and docs/OFFLINE-FIRMWARE.md.
Continue with verified prepared-payload export and card-profile work. Keep bounds
and endian handling explicit. Host AP stamps C0DE rather than calculating the
image CRC; preserve that distinction in later preparation.

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

Save and verify the mandatory timestamped full-flash backup before entering
this operation. Prepare the entire update in RAM before erasing anything.
Validate image addresses, compatibility, PDA patching and companion
primary/station images.
Preserve CIS/PDA storage and save the timestamped full post-operation readback
against the correct physical flash map, comparing every changed/unchanged
range. Confirm restart and identities. Log the last completed phase/block.
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
