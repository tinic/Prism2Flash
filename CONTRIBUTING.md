# Contributing

Prism2Flash is experimental. Read AGENTS.md, ROADMAP.md and docs/PROTOCOL.md
before changing card access. Keep image processing, protocol and the Amiga
backend separate. Permanent flashing is outside the first milestone.

Run `tools/ci.sh host` before submitting changes, and `tools/ci.sh native` for
native changes. Protocol changes need meaningful failure-path checks for
bounds, byte order, deadlines, cancellation and card removal. Never claim a
host model or an empty-slot emulator result as successful hardware testing.

No physical A1200 session is available yet. Do not upload or execute code on
the shared A1200 until the owner explicitly makes it available. See
[docs/HARDWARE-TESTING.md](docs/HARDWARE-TESTING.md) for the later test gate.
Use Amiberry or Copperline for smoke tests and supply your own legal ROM asset.

For bug reports include the full version, command, output, AmigaOS/CPU,
emulator version when relevant, and exact card manufacturer/product IDs and
hardware revision when known. Firmware files and ROMs should not be uploaded.

Project changes are GPL-2.0-only. Preserve upstream notices and checksum pins.
See [docs/RELEASING.md](docs/RELEASING.md) for version and release procedures.
