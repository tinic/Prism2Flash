# Initial implementation baseline — 2026-10-18

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

These results establish compilation and modeled protocol behavior only.
Native timing, Gayle mappings/byte lanes, exact card compatibility, and actual
Genesis entry/exit remain unverified until the hardware gate in ROADMAP.md.

## Current functional boundary

The CLI supports CIS info, active identity/PDA inspection and a four-byte
Genesis RAM probe on a restricted set of working HFA3842 cards. SRAM width
must be supplied explicitly. Ambiguous 800c and damaged/dead-card bootstrap
paths are not enabled. Firmware loading and permanent flashing are subsequent
milestones, not hidden or untested CLI options.
