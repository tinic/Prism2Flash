# Changelog

Versions use Semantic Versioning. Pre-1.0 APIs and card profiles may change.

## Unreleased

### Documentation

- Record the first real A1200 read-only baseline: 15 CLI/CIS-only/offline
  synthetic checks passed with the published 0.2.1-alpha.1 binaries. Preserve
  the full-backup gate; no active card operations or flash access were tested.
- Record the observed 000b:7300 ID, its Linux Host AP/MA401RA source mapping,
  outstanding card-profile/backup work and legacy Version date-display issue.

## [0.8.0-alpha.1] - 2026-10-18

- Implement native MA401 x16 Genesis/bootstrap and SST BF:D5 programming,
  derived from original DOS ILHOPFW.EXE 0.5e and independently reviewed by
  DeepSeek. Pin the initial image; validate compatible user-supplied PRI/STA images and record their hashes.
- Add `Prism2Flash program|verify` and built-in help. Build the intended image
  from the same insertion's fresh PDA, preserve CIS/PDA and all gaps, save/reopen
  it and optionally check an independently prepared expected-image hash.
- Retain persistent destination locks, automatic local timestamps (optional explicit UTC/offset), one exclusive
  claim and one full saved/reopened 8 MiB capture before and after each transaction, checking all 128 KiB aliases.
  Compare the whole exposed interval with the intended image after programming.
- Qualify actual A1200 Genesis double latch, both true byte-write lanes,
  initial-code loading and BF:D5 identification with unchanged full captures.
  Actual SST erase/program and full intended-image readback also passed.
- Correct Gayle odd-byte reads using aligned GETODD; retain true byte writes.
  Clear the NV selector after byte accesses and final word-based flash verify,
  preventing normal CONTROL/CIS reads from seeing stale NV data.
- Keep the live card/RAM session on programming or required readback failure;
  persist the failure before waiting. Record explicit reset-free release and
  actual cleanup separately. Never automatically retry erase/program/reset.
- Address independent review: bounded PDA lookup, active-Genesis RAM scratch
  gate, local byte-write/handoff proof, explicit AUX enable checks, F0 read-mode
  cleanup on identification failure and separate erase-attempt/completion flags.
- Preserve model fault coverage, four-file tamper checks, real short-write and
  descriptor-exhaustion evidence, and native/emulator startup/runtime guards.
- Include all three native executables and their complete corresponding source
  in release packages. No proprietary firmware or DOS binary is distributed.

### Scope

Qualified MA401 V2.5 / PD5LMWP200, pinned initial and validated compatible PRI/STA inputs. Other card
profiles, generic dead-card recovery and a running-primary regular-flash path
remain separate work. Exact hardware runs and their limitations are recorded
in docs/HARDWARE-TESTING.md; emulator results are not hardware validation.

- Put a concise quick start first in README/docs. Generate local timestamps and
  short backup subdrawers from the Amiga clock with `--backup-dir`; timezone
  arguments are optional. Local labels end in L and never claim UTC.
- Replace per-word GetSysTime pacing with a monotonic E-clock wait. Refuse if
  firmware disables AUX mid-capture, preserving its disabled state. Add timer
  wrap, AUX-loss and local-clock boundary tests. Physical timing qualification
  of the pacing change remains pending; the actual upgrade and cold-boot IDs
  are recorded separately from the failed additional reader transactions.

## [0.7.0-alpha.1] - 2026-10-18

### Added

- Host-only owned operation transaction: preflight new output files, claim once,
  acquire repeated NV captures, close/reopen and verify raw files and the before
  manifest, perform the modeled inspect/probe, save a full after capture and
  compare every byte. Keep ownership through the final record and cleanup.
- Separate acquisition, operation, readback, comparison, cleanup and journal
  errors. Attempt post-operation evidence after cancellation or operation
  failure when the same insertion remains accessible; never reset/reclaim.
- Model CLI transaction options `--output NEW_DIRECTORY --utc TIMESTAMP` on
  inspect/probe. Existing protocol-only simulator commands remain available.
- Phase fault tests, filesystem/hash oracles and a real short-write test proving
  failed storage blocks work. Native refusal tests cover transaction options.
- Explicit PD5LMWP200 candidate profile and a read-only saved-capture audit.
  Recheck the original pair, exact 128 KiB aliasing, PDA CRC, NIC tuple and
  configuration without exporting identifiers/calibration or touching hardware.

### Scope

The transaction journal models a synthetic linear 8 MiB NV device. It is built
for host tests only and cannot select a physical card profile. The actual MA401
profile, physical coverage and native operation transaction remain unapproved;
native inspect/probe still refuse before slot access. No new hardware operation,
Genesis hardware validation, bootstrap or programming engine is included.

## [0.6.0-alpha.1] - 2026-10-18

### Added

- Transactional offline preparation on private payload copies: exact PDA plugs,
  source-basename fields, DE C0 signature stamps and PRI/STA merging. Reject
  primary-only flash preparation and overlaps that would lose calibration.
- `Prism2Firmware prepare` reports source/PDA and prepared-segment SHA-256,
  byte counts and entry point. It reads files only; payload export is deferred.
- Byte-exact comparisons with retained Host AP preparation routines on original
  synthetic data, including signature boundary merging and RAM entry selection.
- Verified RAM transfers write each bounded chunk once, then require two
  consecutive readbacks matching the expected bytes. Read retries never rewrite.
  Preserve partial progress and enforce a wrap-safe deadline during transfers.

### Changed

- Genesis probing uses verified RAM readback, preserves an existing AUX session,
  clears OFFSET before disabling AUX, avoids resets before entry begins and
  preserves separate operation/cleanup errors. Check cancellation/removal at
  transfer boundaries, including cleanup. Add fault injection at every modeled
  I/O boundary for both initial AUX states.
- Record the user-supplied MA401 reference label: FCC ID PY3MA401RD, 5 V.
  The user confirmed this is an online reference, not the backed-up unit;
  its printed MAC differs from the saved PDA.
- Bind the subsequently supplied actual-card photo to the backup PDA by exact
  MAC match: MA401 V2.5, FCC PD5LMWP200, 5 V. Keep this filing separate from
  rev.D reference photos. Inspect matching PNG exhibits: U5 SST39VF010,
  90-4C-WH; U4 apparently ISL3873AIK; U1 marking unreadable. The physical map
  and before/after backup gate still require independent review.
  The generic MA401RA/ISL37300P CIS does not select an exact physical memory map.
- Retrieve Intersil's original memory-layout data sheet through playhouse2 and
  record an offline x16 SRAM configuration clue from the existing CRC-valid PDA.
  Neither finding enables the hardware gate or establishes physical flash coverage.
- Inspect the user's FCC photo PDFs: MA401 rev.D labels and an apparent
  SST39VF010 flash marking on the filing sample support a candidate 128 KiB
  profile. SRAM markings remain unreadable; no hardware gate is relaxed.

### Scope

Host/model and native emulator validation; no new physical-card operation.
Physical flash coverage, SRAM width and the fresh before/after backup operation
gate remain unresolved. Native active commands remain blocked. No flash
programming engine, bootstrap execution or prepared-payload export is added.

## [0.5.0-alpha.1] - 2026-10-18

### Changed

- Replace the procedural firmware API with a move-only Image, private Parser,
  immutable segment/metadata views and C++23 std::expected results. Remove the
  raw owning segment arrays, parser callbacks, output pointers and free API.
- Model PDA records as checked borrowed views and card snapshots as value types
  with optional MAC/supplier entries. Parse snapshot fields with string_view and
  from_chars rather than strtok and sentinel buffers.
- Put the protocol implementation in Card methods using spans and typed results.
  Return probe/capture reports that preserve acquisition and cleanup failures.
- Scope file, slot, payload and backend ownership. Keep explicit checked closes
  and hardware restoration where delayed failures must be reported.
- Keep typed heap ownership on the checked C allocator and guard against
  linking the throwing C++ runtime into Kickstart 2.x binaries.
- Add bounded move-aware storage with nonthrowing allocation, ownership/failure
  tests, and native runtime storage checks. Keep C++23/RC17/LLVM formatting.

### Scope

Host and emulator checks only; no real card operation is authorized by this
refactor. The physical-flash backup gate and capture evidence formats remain.

## [0.4.0-alpha.1] - 2026-10-18

### Changed

- Build all project-owned implementation and test sources as C++23. Use scoped
  heap ownership, bounded spans/arrays, constexpr data and C++23 byte swapping.
- Adopt LLVM formatting, pinned clang-format 18.1.8 and a required CI format check.
- Use the official AmigaPorts GCC 16.2-rc17 archives (GCC 16.2.1), with SHA-256
  pins for macOS arm64/Linux x86_64. Validate existing startup and inline fixes
  without modifying the runtime; check allocator object pins across both assets.
- Exercise allocator alignment, reallocation/payload preservation and scoped
  cleanup in the native emulator helper. Keep exceptions and RTTI disabled.

### Scope

This migration does not enable additional card operations. Hardware capture
results from 0.3.0-alpha.1 remain historical; the new binaries require separate
hardware validation. No real card is accessed by migration tests.

## [0.3.0-alpha.1] - 2026-10-18

### Added

- `Prism2Genesis backup --output NEW_DIRECTORY --utc YYYYMMDDTHHMMSSZ`:
  two complete 8 MiB AUX NV address-space reads, byte comparison, saved-file
  reopen verification and SHA-256 manifests in a new timestamped directory.
  Require consecutive whole-page agreement with bounded retries, and count
  disagreements; never majority-vote or repair bytes in captured files.
- Exact MA401RA/ISL37300P CIS matching for acquisition, separate from the
  active-card allowlist. No INIT, Genesis, AUX payload write or programming
  command is used. Clear the AUX selector, disable only an AUX session opened
  by the reader, and restore any temporary COR selection without resetting the
  slot. Never replay unreadable scratch-register values. Preserve partial captures on failure.
- NV-selector/page/bounds, hash-vector, full-read, mismatch, removal,
  cancellation, timeout, file-preservation and native startup checks.

### Fixed

- PDA CRC uses CRC-16/ARC (reflected polynomial 0xa001, initial zero), matching
  the retained Host AP and linux-wlan-ng tables. Earlier synthetic fixtures
  shared an incorrect 0x8408 implementation. An independent fixed CRC oracle
  now checks the parser; previous metadata/payload comparisons did not test PDA CRC.

### Scope

Address-space capture alone does not certify a physical flash map or recovery
image. Active inspection/probing and programming remain blocked. Firmware and
private card dumps are never included in releases. The real MA401RA passed
two complete repeated AUX NV captures, saved-file verification and cleanup;
physical flash coverage and active-operation approval remain unverified.

## [0.2.1-alpha.1] - 2026-10-18

### Changed

- Require fresh, verified, timestamped full physical-flash backups before every
  impactful card operation and timestamped full readbacks afterward. Document
  identity/range/checksum manifests, persistent file verification, failure
  handling and the prohibition on partial/stale backups or force overrides.
- Native active `inspect` and Genesis `probe` now refuse before slot access
  until full backup acquisition/export is implemented and verified. CIS-only
  `info`, offline firmware tools and host protocol simulation remain available.
- Add a refusal-path regression with an aborting backend, and update emulator
  expectations. No hardware access or flash backup implementation is claimed.

## [0.2.0-alpha.1] - 2026-10-18

### Added

- Native and host `Prism2Firmware` for offline image inspection and metadata/PDA
  validation, with an independent portable parser library and no card backend.
- Bounded S-record decoding, fragmented little-endian firmware metadata,
  component/platform/interface checks, RAM/flash class enforcement, and plug
  and signature descriptor validation.
- Saved card snapshots and CRC-checked binary PDA validation, binding both NIC
  identity and MAC address and refusing missing/mismatched calibration plugs.
- Optional PRI/STA pair compatibility checks using the replacement primary's
  supplier range, with no variant bypass or overlapping payloads.
- Original synthetic fixtures, malformed-input stress checks and native emulator
  coverage for the offline utility. Both tools are included in release packages.
- Flash's corrected firmware findings and their source/provenance limits.

### Fixed

- Accept the Intersil RAM-image dialect's S7 entry record before payload records;
  S3 load addresses remain independent from the entry point.
- Reject a malformed PDA terminator before a later valid end record.

### Scope

Validation reads files only and leaves firmware bytes unchanged. Image plugging,
signature stamping, prepared output, merging, hardware address-map approval,
bootstrap execution and flash programming remain later milestones. No physical
Amiga or card testing was performed.

## [0.1.0-alpha.1] - 2026-10-18

### Added

- Native 68000 `Prism2Genesis` for AmigaOS 2.05 or later: CIS information,
  firmware/PDA inspection and an explicit x8/x16 Genesis RAM probe.
- Exclusive card.resource ownership, card-removal handling, bounded hardware
  waits and normal-mode return handling.
- Deterministic host protocol tests and Amiberry/Copperline smoke runner.
- GitHub CI for Linux GCC/Clang, macOS Clang, sanitizers, native cross builds,
  release archive verification and CodeQL analysis of the portable C code.
- Versioned LHA binary package, corresponding source archive, SHA-256 checksums
  and build metadata. Tag releases run the complete CI gate before publication.
- `--version` and an AmigaOS `$VER` identifier from the project VERSION file.

### Fixed

- Bare-system startup failure caused by Newlib's unused double-math formatter
  dependency; integer-only formatting and a build guard prevent recurrence.

### Experimental scope

No real card or physical Amiga has been tested. The versioned executable passed
52 startup/no-card checks across Amiberry and Copperline on A600/68000 and
A1200/68020 profiles. This tool requires working primary firmware and valid
CIS/PDA data. It does not load bootstrap firmware, recover dead cards, back up
firmware, or program flash. Keep real-card testing deferred until the A1200 is
available; consult ROADMAP.md and docs/HARDWARE-TESTING.md.
