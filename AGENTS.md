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
