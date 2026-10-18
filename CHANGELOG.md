# Changelog

Versions use Semantic Versioning. Pre-1.0 APIs and card profiles may change.

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
