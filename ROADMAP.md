# Roadmap

## Active execution plan — 2026-10-18

**Finish condition:** the actual A1200 MA401 runs the selected newer firmware;
full before/after captures and running identities verify it; Claude receives the
results and can resume the dependent bug tests.

1. **In progress:** reverse engineer original DOS ILHOPFW.EXE 0.5e (2002-04-24),
   with DeepSeek assigned the entry/detection/bootstrap/write routines. Derive
   the Amiga sequence from those routines and document platform adaptations.
2. Finish the native owned transaction: persistent destination locks, checked
   phase UTC, dual full captures, close/reopen/hash verification and exact card
   profile binding. The human accepts the full 8 MiB AUX NV layout for the RAM
   probe; do not reopen the physical-layout debate.
3. Run one real x16 Genesis probe with fresh before/after captures under the
   same claim. Require RAM sentinel verification, normal firmware response and
   unchanged NV. Fix the concrete failure if any; do not blindly try another
   width, reset, reclaim or restore.
4. Implement the DOS-derived initial-image loader and programming engine;
   validate the selected initial/primary/station images against this exact card.
5. Perform the firmware update with fresh captures, verify intended changes and
   preserved PDA/CIS, restart through the verified path, read firmware identities
   and confirm normal driver use. Preserve the live recovery session on failure.
6. Report hardware results to Claude and return the reserved A1200. Finish the
   matching committed source/release and remove task staging.

**Evidence already obtained:** original DOS EXE/INI/help/documents downloaded and
hashed; DOS help executed in DOSBox; idle sentinel routine identified at
EXE file 0x10423 (load-relative segment 0a22:3803). Fresh A1200 backup at
20261018T211840Z passed: two 8 MiB raw files, saved-file verification, cleanup and
CIS recheck; SHA-256 matches the previous untouched backup. No new INIT, Genesis
or flash programming has yet been performed in this session.

The goal stays open through implementation and failed experiments. CI, emulator
checks and releases support it; none is the completion condition. Stop card I/O
on sticky removal, an unverified backup, or a failed recovery state; continue
analysis and the specific fix, and report the exact failure.

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
- [x] Exercise the owned before/after transaction with a synthetic NV profile,
      durable-file checks, storage/phase fault injection and unchanged-byte
      comparison in the host model. Native implementation/approval is pending.
- [x] Record the exact PD5LMWP200 candidate and reproducibly audit existing
      captures for matching raw bytes, aliases, CRC and PDA configuration.
- [x] Address independent model review: CIS/PDA/anchor audit, four-file tamper
      regression, real output-open and short-write failure evidence.
- [x] Cross-build the shared transaction ordering for Amiga and test native
      admission refusal with an aborting backend. Active admission stays closed.
- [x] Implement native clock/offset and resolved-volume diagnostic preflight,
      including RAM-assign refusal; validate policy offline and APIs in emulators.
- [ ] Retain destination locks, bind an approved physical map and record per-phase
      checked UTC times in a native file journal, then independently review it.
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
