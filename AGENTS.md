# Prism2Flash development

- This is a separate project from AmiNetXDuo. Do not edit the AmiNetXDuo
  repository as part of this project unless explicitly requested.
- Hardware testing is deferred by the user. The A1200 is busy with another
  project. Do not SSH to it, upload binaries, reset cards, or run hardware
  commands until the user says the machine is available for testing.
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
