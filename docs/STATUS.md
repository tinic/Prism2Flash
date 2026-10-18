# Implementation and emulator baseline — 2026-10-18

Implemented the first Genesis probe milestone in the separate Prism2Flash
repository. There has been no A1200 connection, deployment or hardware test.

## Verified locally

- Host build: Apple Clang 21, C99, warnings treated as errors.
- CTest: all six registered checks passed. The protocol check includes x8/x16
  mode selection, payload byte order and page boundaries, malformed RID/PDA
  rejection, CRC corruption, busy/AUX timeouts, timer wrap, readback mismatch,
  and removal/cancellation injection across protocol I/O boundaries.
- Simulator `inspect` completed through identity and PDA validation.
- Native cross build: m68k-amigaos GCC 16.2.0, `-m68000 -O2`, warnings treated
  as errors. `file` identifies the output as an AmigaOS loadseg executable.
- Every retained third-party artifact matches third_party/SHA256SUMS.
- Versioned 0.1.0-alpha.1 executable: 52 smoke checks passed across Amiberry 8.4.1 and
  Copperline 1.0.0, each with A600/68000/Kickstart 2.05 and
  A1200/68020/Kickstart 3.1 profiles and a 4096-byte Shell stack.
- Fixed a pre-main startup failure by using Newlib integer-only formatting;
  a native build guard rejects the unwanted IEEE double-library dependency.
  See [EMULATOR-RESULTS.md](EMULATOR-RESULTS.md) for evidence and reproduction.

These results establish compilation, modeled protocol behavior, native startup
and clean no-card failure handling.
Native timing, Gayle mappings/byte lanes, exact card compatibility, and actual
Genesis entry/exit remain unverified until the hardware gate in ROADMAP.md.

## Current functional boundary

The CLI supports CIS info, active identity/PDA inspection and a four-byte
Genesis RAM probe on a restricted set of working HFA3842 cards. SRAM width
must be supplied explicitly. Ambiguous 800c and damaged/dead-card bootstrap
paths are not enabled. Firmware loading and permanent flashing are subsequent
milestones, not hidden or untested CLI options.

## Repository and release infrastructure

GitHub CI now covers Linux GCC/Clang and macOS Clang host builds, sanitizer
checks, pinned native cross builds, and independently verified release packages.
CodeQL scans the portable C code. VERSION, RELEASE_DATE and CHANGELOG.md drive
tagged prereleases with binary/source archives, checksums and build metadata.
See RELEASING.md for the release gate and maintenance procedure.

## Offline parser/validation milestone — 0.2.0-alpha.1

Prism2Firmware now performs bounded image inspection and metadata/PDA checks
on host and native Amiga builds, with no card backend. Flash's corrected image
classification and entry/load-address findings are documented in
FIRMWARE-FINDINGS.md. Parsed metadata and every payload byte match Host AP
0.4.7 for seven privately supplied, checksum-pinned images. Normal CI uses only
original synthetic fixtures; no firmware image is distributed.

Passing validation leaves input bytes unchanged. Preparation, merging, signature
stamping and hardware-specific address-map approval remain unimplemented.
The physical hardware gate is still open and testing remains deferred.

The 0.2.0-alpha.1 binaries passed 78 native guest checks at 4 KiB stack: the
offline tool on all four Amiberry/Copperline A600/A1200 profiles, plus Genesis
startup/no-card regressions on Amiberry A600 and Copperline A1200. Host and
sanitizer CTest runs passed all ten registered checks, including 17 offline
CLI test groups, 120 deterministic input mutations and the C API byte/ownership
checks. Detailed current binary hashes are in EMULATOR-RESULTS.md.

## Mandatory-backup gate — 0.2.1-alpha.1

The user now requires a fresh, timestamped full physical-flash backup before
any impactful card operation, with a timestamped full readback afterward.
Native active inspection and probing refuse before slot access until backup
acquisition/export is implemented and independently verified. CIS-only `info`
and offline file validation remain available; host simulation retains the
protocol paths for development. Earlier executable/emulator results above are
historical and do not imply this new gate has backup acquisition implemented.
See HARDWARE-TESTING.md for the complete acquisition and verification contract.
No hardware has been accessed.

The 0.2.1-alpha.1 native executables passed 52 guest checks across Amiberry
A600/68000 and Copperline A1200/68020, including mandatory-backup refusal,
CIS-only empty-slot behavior and offline synthetic firmware validation.
All eleven host CTests passed, including the aborting-backend gate regression.
Exact binaries and outputs are retained in EMULATOR-RESULTS.md.
