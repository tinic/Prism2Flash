# Prism2Flash development

- This is a separate project from AmiNetXDuo. Do not edit the AmiNetXDuo
  repository as part of this project unless explicitly requested.
- The user made the A1200 available on 2026-10-18 and subsequently requested
  that it remain reserved for this project after the first read-only checks.
  Check the latest human handoff before future hardware work. Use its existing
  HTTP control path; SSH is a recovery fallback. The full-backup prerequisite
  below still blocks active inspection, reset, Genesis and programming.
- Implement Genesis tooling first; a normal flasher follows it. See ROADMAP.md.
- Keep image processing, the card protocol and the Amiga backend separate.
- Never describe simulated results or cross compilation as hardware validation.
- Use Amiberry and Copperline for emulator smoke tests. Do not use FS-UAE.
- Preserve source provenance and GPL notices. Do not redistribute firmware
  images without establishing their distribution terms.
- Test byte order, alignment, bounds, timeouts and failure cleanup. Never use
  unbounded hardware polling or CPU-speed-dependent delay loops.
- Keep slot-removal state sticky. A newly inserted card must never inherit an
  operation intended for the previous card. Do not steal driver ownership.
- The initial probe has no flash programming path. Adding flash requires the
  corresponding roadmap milestone and its independent verification.
- Keep the original pccard source archive alongside any distributed binary.
- VERSION is the single public version; update RELEASE_DATE and CHANGELOG.md
  for releases. Tags must match it exactly. See docs/RELEASING.md.
- Run tools/ci.sh host and tools/ci.sh native for native changes. Release
  packages require a clean checkout and include complete corresponding source.

- User requirement: every impactful card operation requires a fresh, timestamped
  full physical-flash backup verified and saved before the operation, followed
  by a timestamped full readback afterward when the card remains accessible.
  Apply this to active inspection/initialization, resets, Genesis RAM probing,
  bootstrap loading, and flash/restore operations. PDA/CIS-only saves do not
  satisfy it. See docs/HARDWARE-TESTING.md for the required backup contract.
- Until full backup acquisition/export and its operation gate are implemented
  and verified, native inspect/probe must refuse before opening the slot.
  Do not add force/skip-backup options or accept an old backup as authorization.
- Human handoff, 2026-10-18 21:22 UTC: hardware testing is authorized now.
  The user explicitly accepts the full 8 MiB AUX NV layout for the MA401 x16
  RAM-only Genesis probe and requires that the layout question not be reopened.
  Implement and verify the owned fresh before/after transaction, then perform
  the test. Derive entry/detection/bootstrap/programming behavior from the DOS
  ILHOPFW.EXE; DeepSeek was explicitly assigned its reverse engineering.
  This updates the earlier layout/admission hold for that RAM-only test. Full
  fresh saved-file-verified captures, sticky removal and no blind reset/retry
  remain required. No firmware programming routine is implemented yet.

- Project-owned implementation and native tests use C++23 with exceptions and
  RTTI disabled. Follow LLVM formatting via pinned clang-format 18.1.8 and
  tools/ci.sh format/format-check. Do not reformat retained third-party sources.
- Native builds use the official AmigaPorts GCC v16.2-rc17 pin. Preserve runtime
  verification gates; do not apply legacy binary repairs to this compiler.

- Human priority, 2026-10-18: focus on real Genesis and firmware upgrade. Do
  not spend active hardware time monitoring, discussing or optimizing CI. Run
  only the targeted local checks needed to use changed hardware code safely.

- Hardware qualification, 2026-10-18 22:21 UTC: exact MA401 native bootstrap
  transaction fa2f459 / 20261018T221517Z passed both byte-write lanes, all
  3434 initial bytes, BF:D5 ID and four equal full 8 MiB captures with saved
  file verification and final CIS. DeepSeek's implementation review confirmed
  the DOS host algorithm; its remaining cleanup/AUX/diagnostic findings were
  fixed and tested. Proceed with the user's authorized firmware update using
  the same exact card profile, pinned images, fresh owned captures and intended
  image hash. Do not reopen the accepted layout or ask for that permission again.

- Human update, 2026-10-18: four captures are excessive when checksums match.
  Native transactions now require one full timestamped before capture and one
  full after capture, saved/reopened SHA-256 verification and intended-image
  comparison. Validate all 64 aliases against the canonical 128 KiB image;
  reject disagreements without voting, repair or silent retry. This supersedes
  older four-file/repeated-full-capture requirements for the qualified MA401.
  Confirm Claude's WPA2 firmware target, complete hardware verification, then
  finalize a usable tool accepting compatible user-supplied firmware. Put a
  short step-by-step guide first in README and docs; minimize prose.
