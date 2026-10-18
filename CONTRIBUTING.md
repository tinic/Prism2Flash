# Contributing

Prism2Flash is experimental. Read AGENTS.md, ROADMAP.md and docs/PROTOCOL.md
before changing card access. Keep image processing, protocol and the Amiga
backend separate. Permanent flashing is outside the first milestone.

Run `tools/ci.sh host` before submitting changes, and `tools/ci.sh native` for
native changes. Protocol changes need meaningful failure-path checks for
bounds, byte order, deadlines, cancellation and card removal. Never claim a
host model or an empty-slot emulator result as successful hardware testing.

The A1200 is reserved for this project. Hardware work needs the current human
handoff and the backup gate in [HARDWARE-TESTING.md](docs/HARDWARE-TESTING.md).
Formatting/compiler changes use host and emulator checks only. Use Amiberry or
Copperline with your own legal ROM asset; do not use FS-UAE.

## C++ and formatting

Project-owned implementation and tests use **C++23**, with exceptions and RTTI
disabled on every build. Keep explicit allocation-error handling, use local
RAII for ownership, `std::array`/`std::span` for bounded buffers and `nullptr`.
Use C++ casts at OS callbacks/allocator boundaries. Hardware cleanup still
needs explicit status checks, deadlines and sticky removal handling; a
successful destructor is not proof of card recovery. Keep hardware register
semantics unchanged unless supported by protocol evidence.

Use the **LLVM** style in `.clang-format` (two spaces, 80 columns). Do not pack
multiple statements onto a line. The formatting script covers `src`, `include`
and C++ tests; retained upstream sources remain unmodified. The C ABI of the
Amiga OS is preserved at system headers/assembly entry points.

```sh
python3 -m venv build-format-env
build-format-env/bin/python -m pip install clang-format==18.1.8
CLANG_FORMAT=build-format-env/bin/clang-format tools/ci.sh format
CLANG_FORMAT=build-format-env/bin/clang-format tools/ci.sh format-check
CXX=clang++ tools/ci.sh host
CXX=clang++ tools/ci.sh sanitize
tools/ci.sh native
```

Formatting is a required CI job. The formatter version is pinned to 18.1.8
so contributors and CI produce the same output. The native compiler is the
SHA-256-pinned official AmigaPorts **v16.2-rc17** (GCC 16.2.1). Its existing
startup/inline fixes are checked without patching the runtime. The allocator
pins cover both published host archives; emulator smoke tests also exercise
alignment, realloc preservation and scoped buffer cleanup.

For bug reports include the full version, command, output, AmigaOS/CPU,
emulator version when relevant, and exact card manufacturer/product IDs and
hardware revision when known. Firmware files and ROMs should not be uploaded.

Project changes are GPL-2.0-only. Preserve upstream notices and checksum pins.
See [docs/RELEASING.md](docs/RELEASING.md) for version and release procedures.
