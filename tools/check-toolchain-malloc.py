#!/usr/bin/env python3
"""Verify the pinned newlib allocator in every installed toolchain multilib.

The 16.2.2 packages shipped a bootstrap libc despite pinning fixed newlib
source. For this immutable asset series, compare installed malloc.o bytes
against the known-good newlib build, in both libc.a and libg.a. A future
source, compiler, or flag change must deliberately update this map and its
runtime verification; unknown bytes fail closed.
"""

import hashlib
import pathlib
import subprocess
import sys


# Paths are relative to <prefix>/m68k-amigaos/lib. The paired libc/libg
# archive members must match for each variant. Eight distinct hashes cover
# eleven multilibs produced by the pinned newlib source's final make step.
# Official AmigaPorts v16.2-rc17 asset bytes are pinned in toolchain.json.
# RC17 hashes independently agree in libc.a/libg.a
# across both hash-verified official Darwin and Linux archives. Startup and
# allocator runtime checks run in the native emulator helper.
EXPECTED_MALLOC_SHA256 = {
    "": "f5d9eb986c1bfa30bda9043c7bed2ae8778268aeb216ceda83a0cb1840015ad2",
    "libb": "0a082f0d8742a605271fa57be86394d5ec1c6cee7b3310b5e0ff3e09eb8216d0",
    "libm020": "0ebe5f80cd83149070453ff211eb527540015897ed6ccae7ef6602611b1df216",
    "libm060": "7d6109741ec975d7748364c7e288f40553c545dd19db5e6016711845cbf86dab",
    "libm020/libm881": "0ebe5f80cd83149070453ff211eb527540015897ed6ccae7ef6602611b1df216",
    "libb/libm020": "abeffb21330260c509866956f2ccfa5b356404a4c2ac5c70f07d3ac80d478d03",
    "libb/libm060": "885869098305a7bdc89bd7b7740f92ce728a577352ab57f63092689329c0fe9f",
    "libb/libm020/libm881": "abeffb21330260c509866956f2ccfa5b356404a4c2ac5c70f07d3ac80d478d03",
    "libb32/libm020": "485221a4f8f87b9e1c69fe501f97d8991b52a1b1c2b03600a3544e90bb1b9caf",
    "libb32/libm060": "6e6c9d04fbf03e34a15a9e60adf87f9c22d8c0779dfe5701cee48e1fe917c1cf",
    "libb32/libm020/libm881": "485221a4f8f87b9e1c69fe501f97d8991b52a1b1c2b03600a3544e90bb1b9caf"
}


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <toolchain-prefix>", file=sys.stderr)
        return 2
    prefix = pathlib.Path(sys.argv[1]).resolve()
    ar = prefix / "bin/m68k-amigaos-ar"
    gcc = prefix / "bin/m68k-amigaos-gcc"
    libdir = prefix / "m68k-amigaos/lib"
    if not ar.is_file() or not gcc.is_file() or not libdir.is_dir():
        print(f"missing cross-gcc, cross-ar or library directory under {prefix}", file=sys.stderr)
        return 1

    failures = []
    multilibs = subprocess.run(
        [str(gcc), "-print-multi-lib"], check=False, capture_output=True, text=True
    )
    if multilibs.returncode:
        print("cross-gcc could not enumerate multilibs", file=sys.stderr)
        return 1
    compiler_variants = set()
    for line in multilibs.stdout.splitlines():
        if line.strip():
            variant = line.split(";", 1)[0]
            compiler_variants.add("" if variant == "." else variant)
    if compiler_variants != set(EXPECTED_MALLOC_SHA256):
        failures.append(
            "compiler multilib paths differ from pinned allocator map: "
            f"{sorted(compiler_variants)}"
        )

    for archive_name in ("libc.a", "libg.a"):
        expected = {
            str(pathlib.PurePosixPath(variant) / archive_name)
            for variant in EXPECTED_MALLOC_SHA256
        }
        actual = {
            archive.relative_to(libdir).as_posix()
            for archive in libdir.rglob(archive_name)
        }
        for extra in sorted(actual - expected):
            failures.append(f"m68k-amigaos/lib/{extra}: unexpected unverified multilib")
    for variant, want in EXPECTED_MALLOC_SHA256.items():
        for companion in ("crt0.o", "libm.a"):
            path = libdir / variant / companion
            if not path.is_file():
                failures.append(f"{path.relative_to(prefix)}: missing multilib companion")
        for archive_name in ("libc.a", "libg.a"):
            archive = libdir / variant / archive_name
            if not archive.is_file():
                failures.append(f"{archive.relative_to(prefix)}: missing archive")
                continue
            result = subprocess.run(
                [str(ar), "p", str(archive), "malloc.o"],
                check=False,
                capture_output=True,
            )
            if result.returncode or not result.stdout:
                failures.append(f"{archive.relative_to(prefix)}: missing malloc.o")
                continue
            got = hashlib.sha256(result.stdout).hexdigest()
            if got != want:
                failures.append(
                    f"{archive.relative_to(prefix)}: malloc.o {got}, want {want}"
                )

    if failures:
        print("toolchain allocator verification FAILED:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print("toolchain allocator verification passed: 11 libc and 11 libg multilibs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
