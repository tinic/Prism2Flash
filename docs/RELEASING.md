# Versioning, CI and releases

## Version files

`VERSION` is the only source of the full public version. Use canonical SemVer
such as `0.1.0-alpha.1`, `0.1.0` or `0.2.0`. `RELEASE_DATE` contains the fixed
release date as `YYYY-MM-DD`. CMake generates the CLI version and Amiga `$VER`
string from these files. Amiga's numeric version/revision is major.minor;
`Version FILE` also shows the full SemVer after the date. Patch/prerelease
identities must be compared using the full version string.

Every release needs a matching dated `CHANGELOG.md` heading. Tags are exactly
`v<VERSION>`. The metadata job rejects mismatched tags before building.

## Continuous integration

Every push to main or a codex branch, pull request, and manual CI run performs:

- Host protocol/CLI and release-format tests on Linux GCC, Linux Clang and
  macOS Clang, with warnings treated as errors.
- AddressSanitizer/UndefinedBehaviorSanitizer checks on Linux Clang.
- Native 68000 cross compilation on Linux with the SHA-256 pinned compiler.
- Native startup dependency guard, third-party checksum verification, packaging
  and independent Lhasa extraction/CRC verification. The uploaded artifact is
  downloaded and verified again so artifact-action changes are covered by CI.
- Upload of binary/source packages as the `prism2flash-release` CI artifact.

CodeQL separately analyzes the portable C implementation on pushes, pull
requests and a weekly schedule. It does not analyze the Amiga-specific backend
through a host compiler. Dependabot proposes pinned-action updates weekly.
Workflows use read-only repository permissions except the release publication
and CodeQL upload jobs. External actions are pinned to full commit hashes.

Amiberry/Copperline tests require privately supplied Kickstart assets and stay
local for now. They are not claimed as a hosted CI gate. FS-UAE is excluded.
Real-card testing stays subject to the hardware gate in ROADMAP.md.

## Local equivalents

Requires CMake 3.24+, Python 3.9+, a C compiler, curl and tar. Lhasa (`lha`)
is needed for independent package verification. Install `lhasa` with apt or
Homebrew. Package generation itself uses Python's standard library.

```sh
tools/ci.sh host
CC=clang tools/ci.sh sanitize
tools/ci.sh native
tools/ci.sh package
```

The native stage fetches the compiler into `~/.cache/prism2flash/toolchain`,
verifies its pinned archive hash, applies/checks the retained startup and NDK
ABI repairs, and verifies every multilib's allocator. No sibling repository
is required. `AMIGA_TOOLCHAIN_ROOT` can select an existing compatible compiler;
maintainers must run the repair/check tools when supplying one manually.
`tools/toolchain.json` records upstream asset pins and repair provenance.

Packaging requires a clean checkout and uses all Git-tracked files for the
complete source archive. It rejects an executable with a stale version,
non-HUNK format or double-math dependency. Archives normalize dates, ordering
and source ownership; repeating packaging on the same checkout/build yields
identical files. `--allow-dirty` is available only for local development checks;
its output is marked dirty and must not be published.

The source archive can rebuild the native executable without `.git`; only
creating a new release archive needs a Git checkout. It contains SOURCE.json
with the source commit and epoch, plus all retained third-party sources.

## Publish a release

1. Update `VERSION`, `RELEASE_DATE` and the matching changelog entry.
2. Run host/native/package checks. Run available Amiberry/Copperline smoke
   profiles for any changed binary and record the exact hash and limitations.
3. Commit and push to main. Wait for CI and CodeQL to pass.
4. Tag that commit, then push the tag:

   ```sh
   git tag -a "v$(cat VERSION)" -m "Prism2Flash $(cat VERSION)"
   git push origin "v$(cat VERSION)"
   ```

The Release workflow checks metadata, runs the reusable CI jobs for that exact
tag, downloads their verified packages, checks download hashes and creates a
GitHub Release. Versions containing `-` become prereleases and do not replace
the latest stable release. No release is created on a failed gate.

Assets are the Amiga 68000 LHA, complete source tar.gz, SHA256SUMS,
RELEASE-NOTES.md and build-info.json. The binary bundle includes original
pccard sources and notices. Source and binaries must remain available together.
Releases contain no firmware or ROM images. Do not reuse a published version or
replace its assets; fix an error in a new version.
