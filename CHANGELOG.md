# Changelog

Versions use Semantic Versioning. Pre-1.0 APIs and card profiles may change.

## Unreleased

### Documentation

- Record the first real A1200 read-only baseline: 15 CLI/CIS-only/offline
  synthetic checks passed with the published 0.2.1-alpha.1 binaries. Preserve
  the full-backup gate; no active card operations or flash access were tested.
- Record the observed 000b:7300 ID, its Linux Host AP/MA401RA source mapping,
  outstanding card-profile/backup work and legacy Version date-display issue.

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
