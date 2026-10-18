# Prism2Flash

[![CI](https://github.com/tinic/Prism2Flash/actions/workflows/ci.yml/badge.svg)](https://github.com/tinic/Prism2Flash/actions/workflows/ci.yml)
[![CodeQL](https://github.com/tinic/Prism2Flash/actions/workflows/codeql.yml/badge.svg)](https://github.com/tinic/Prism2Flash/actions/workflows/codeql.yml)
[![Release](https://img.shields.io/github/v/release/tinic/Prism2Flash?include_prereleases)](https://github.com/tinic/Prism2Flash/releases)

Native Amiga utilities for Intersil Prism II/2.5/3 PCMCIA cards, developed
alongside AmiNetXDuo. The project starts with Genesis recovery and will then
add a regular firmware flasher using the same programming engine.

**Status: Genesis protocol model and offline firmware parser/validator/preparation
implemented. Native startup, offline synthetic-file checks and CIS-only
identification passed on the real A1200, alongside Amiberry/Copperline checks.**
Repeated AUX NV capture passed on one card reporting MA401RA CIS with bounded page
agreement and saved-file SHA-256 verification. Physical flash coverage remains
unverified. Active card inspection, Genesis
and programming stay blocked pending verified full backups. The user has reserved
the A1200 for this project. See [the hardware record](docs/HARDWARE-TESTING.md).

## Downloads and project workflow

Get versioned Amiga binaries from [Releases](https://github.com/tinic/Prism2Flash/releases).
Each release includes an Amiga 68000 LHA, complete corresponding source,
checksums and compiler/source metadata. See [installation](docs/INSTALL.md),
[changelog](CHANGELOG.md), [contributing](CONTRIBUTING.md) and
[versioning/CI/release procedures](docs/RELEASING.md).

`VERSION` drives `Prism2Genesis --version` and the embedded Amiga `$VER`
identifier. Version tags run host tests, sanitizers, native compilation and
archive verification before publishing. Initial releases are experimental
prereleases while real-card validation is pending.

## Mandatory backups before active card operations

Every impactful operation must save a fresh, verified, timestamped **full flash**
backup first, followed by a timestamped full readback afterward. This includes
active inspection, resets and Genesis RAM probes, as well as future loading,
flashing and restores. PDA/CIS-only backups are insufficient. Backup failure
blocks the operation; no skip/force option is permitted.

**Native `inspect` and `probe` are currently blocked before slot access** until
full-flash backup support is implemented and verified. `info` and offline file
inspection/validation remain available. Releases through 0.2.0-alpha.1 lack
this safeguard; do not run their active commands on cards. See the
[backup contract](docs/HARDWARE-TESTING.md#mandatory-full-flash-backups).

## First executable: Prism2Genesis

This first milestone establishes PCMCIA ownership, AUX memory access and the
Genesis reset sequence. The protocol model implements the following commands;
native active commands are currently gated as described above:

- `info`: claim the slot and report the CIS manufacturer/product identifiers.
  This does not configure the card or issue Prism commands.
- `backup --output NEW_DIRECTORY --utc YYYYMMDDTHHMMSSZ`: experimentally
  capture the entire 8 MiB AUX NV address space twice, compare bytes and reopen
  saved files to verify SHA-256. Requires a new persistent timestamped directory
  and the exact MA401RA/ISL37300P CIS strings. Partial files are retained. This
  does not certify physical flash coverage or enable active commands.
- `inspect`: configure the I/O window, initialize the card, read NIC/primary/
  station identities and validate a 1024-byte PDA read including its CRC.
- `probe --sram 8` or `probe --sram 16`: perform those checks, enter Genesis,
  run the Host AP four-byte SRAM readback test at `0x7e0038`, return to normal
  mode, and check that primary firmware responds again.

The host simulator can exercise the owned before/after backup transaction:

```sh
build-host/Prism2GenesisSim probe --sram 16 \
  --output /persistent/path/probe-20261018T120000Z --utc 20261018T120000Z
```

This saves two before and two after captures, verifies saved raw files and
manifests, and compares all modeled NV bytes while retaining slot ownership.
The synthetic 8 MiB model profile cannot approve physical-card coverage.
Native active commands refuse even with these options. See
[transaction behavior and remaining gates](docs/OPERATION-TRANSACTION.md).

For read-only analysis of existing MA401 captures, use
`python3 tools/audit_nv_capture.py FIRST SECOND`. Its report checks the saved-capture CIS/NIC/PDA, alias and firmware-anchor
[candidate observations](docs/evidence/ma401-pd5lmwp200-candidate.json);
saved files never authorize a new operation.

Native builds also provide `preflight --output NEW_DIRECTORY --utc TIMESTAMP
--utc-offset MINUTES_EAST_OF_UTC`. It checks the Amiga clock against the supplied
reference and resolves storage through assigns, without creating files or
accessing the card. A passing diagnostic cannot authorize an operation; see
[preflight limits](docs/OPERATION-TRANSACTION.md#native-diagnostic-preflight-08).

The probe resets the card and changes four bytes in its RAM. It requires
exclusive slot ownership and interrupts any use of that card for networking.
`--sram` means the card's internal SRAM width, **not** the Amiga's PCMCIA bus
width. Determine it from documentation for the exact hardware revision.
There is no automatic width guessing or force option.

The current program requires working primary firmware, readable CIS data and
a valid PDA. It does not yet load bootstrap firmware, revive a dead card or
program flash. The experimental capture command is described in
[the hardware record](docs/HARDWARE-TESTING.md). Those steps are explicit milestones in
[ROADMAP.md](ROADMAP.md). No command `0x22` programming path is compiled into
the executable.

## Offline tool: Prism2Firmware

`Prism2Firmware inspect IMAGE` decodes a Prism S-record image and reports its
component, RAM/flash class, platforms, interface ranges, PDA plugs and signature
descriptors. `validate` checks it against a saved NIC/MAC/supplier snapshot and
a CRC-checked binary PDA, with optional replacement-primary compatibility.
`prepare` applies PDA plugs, source filename fields and C0DE stamps to private
copies, merges an optional PRI/STA pair and reports byte counts and SHA-256.
It reads files only and leaves source/PDA files unchanged. No card backend is linked.

See [offline commands and data formats](docs/OFFLINE-FIRMWARE.md) and
[Flash's incorporated findings](docs/FIRMWARE-FINDINGS.md). The parser was
compared byte-for-byte with Host AP on seven privately supplied image variants;
normal CI uses original synthetic fixtures and includes no proprietary firmware.
Prepared-payload export, bootstrap loading and actual flashing remain later milestones.

## Native build

Requires CMake 3.24+ and a C++23 `m68k-amigaos-g++` cross toolchain with Amiga NDK headers.
The pinned native compiler is AmigaPorts v16.2-rc17 (GCC 16.2.1).
Project sources use LLVM formatting enforced by CI; see [contributing](CONTRIBUTING.md).
The target is 68000 with AmigaOS 2.05 or later. Hardware compatibility with
that minimum OS is still to be tested.

```sh
export AMIGA_TOOLCHAIN_ROOT=/path/to/m68k-amigaos-toolchain
cmake -S . -B build-amiga -DCMAKE_TOOLCHAIN_FILE=cmake/amiga.cmake
cmake --build build-amiga
```

The Amiga executables are `build-amiga/Prism2Genesis` and
`build-amiga/Prism2Firmware`. The host offline executable is `build-host/Prism2Firmware`.
For a pinned compiler with its startup/ABI checks applied automatically, run
`tools/ci.sh native` on Linux x86_64 or macOS arm64. This does not require an
AmiNetXDuo checkout. Host checks use `tools/ci.sh host`.
`backup`, `inspect` and `probe` require `utility.library` and **pccard.library 2.2 or
newer**. An unmodified pccard.library 2.2 is provided in
`third_party/pccard/Libs`, together with its complete original source archive
and license notices. Do not replace a newer installed library.

The initial configured-card allowlist is deliberately narrow: CIS identifiers
`0274:1612`, `0274:1613`, `0250:0002`, `028a:0002`, `02aa:0002`,
`02d2:0001`, followed by a second check of the running NICID. This list comes
from upstream code; it is **not a hardware-tested compatibility list**.
Ambiguous NICID `800c`, early HFA3841 cards, multifunction cards and Prism 3
SSF cards await dedicated profiles. A 3.3 V-only or CardBus card is not a
target for the A600/A1200 slot.

## Host verification

```sh
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host
ctest --test-dir build-host --output-on-failure
build-host/Prism2GenesisSim probe --sram 8
```

`Prism2GenesisSim` is a deterministic protocol model. It does not connect to
an Amiga, emulate Gayle or establish real-card compatibility. The tests check
both SRAM control values, AUX page boundaries and byte order, RID/PDA
validation, deadlines including clock wrap, incorrect SRAM selection,
readback errors, removal/cancellation, and rejection of flash commands.

See [docs/HARDWARE-TESTING.md](docs/HARDWARE-TESTING.md) for the deferred test
procedure, [docs/PROTOCOL.md](docs/PROTOCOL.md) for the implementation contract,
and [docs/SOURCES.md](docs/SOURCES.md) for provenance and research.

## Emulator smoke tests

Version 0.2.0-alpha.1 passed 78 native checks using a 4096-byte Shell stack:
52 offline file-processing checks on Amiberry/Copperline with A600/68000 and
A1200/68020 profiles, plus 26 Genesis startup/no-card regression checks on
Amiberry A600 and Copperline A1200. No physical card or Amiga was used.

The smoke test caught an unnecessary Newlib double-math library dependency
that prevented startup on a bare system. The native CLI now uses integer-only
formatting, and the build rejects that dependency if it reappears.

`tests/emulator_smoke.py` creates a fresh isolated drive and records exit
codes, output and binary/ROM hashes. It uses Copperline's direct executable
launch interface, or a bare boot drive in Amiberry. Supply your existing ROM
assets; none are copied into the repository. FS-UAE is not used by this runner.

```sh
python3 tests/emulator_smoke.py --suite firmware --emulator copperline \
  --emulator-path /path/to/copperline --model A600 --cpu 68000 \
  --rom /path/to/matching-a600-kickstart.rom
python3 tests/emulator_smoke.py --emulator amiberry \
  --emulator-path /path/to/Amiberry --model A1200 --cpu 68020 \
  --rom /path/to/matching-a1200-kickstart.rom
```

See [docs/EMULATOR-RESULTS.md](docs/EMULATOR-RESULTS.md) for the recorded result
and its limits. Physical card testing remains deferred.

## License

Project source is GPL-2.0-only; see [COPYING](COPYING). Third-party code and
materials retain their own notices. No Intersil firmware image is bundled.

## Implementation

See [C++ design](docs/CPP-DESIGN.md) for ownership, typed results, bounded
storage, card interfaces and the Amiga ABI boundary.
