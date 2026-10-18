# Delivery

Prism2Flash provides native Amiga card backup, Genesis/bootstrap, firmware
programming and post-boot verification, plus offline firmware preparation.
The short [user guide](QUICKSTART.md) is first in the README.

## Implemented

- Documented Prism2 PC Card detection from prism2.device and Host AP.
- Card-specific PDA identity and x8/x16 SRAM configuration; runtime RAM detection.
- Original DOS Genesis double latch, initial-code loading and RAM verification.
- DOS AMD, ST, SST and Atmel parallel-flash programming and complete image checks.
- PRI/STA compatibility validation, PDA calibration plugging and CIS preservation.
- Automatic full before/after backups, saved-file checksums and unique output names.
- Optional date labels; no clock-setup requirement.
- Bounded failure handling and retained recovery session without automatic retries.
- C++23, official AmigaPorts GCC16.2-rc17, versioned binaries and corresponding source.
- Host fault models and Amiberry/Copperline startup checks.

## Completed MA401 upgrade

- Original DOS ILHOPFW.EXE behavior reverse engineered and independently reviewed.
- Actual A1200 x16 Genesis, both byte-write lanes and initial-code loading passed.
- Actual card programmed once with PRI1.1.1 / STA1.7.4; every flash byte verified.
- Two complete saved 8 MiB after-readbacks matched the intended image exactly.
- Running NIC/PRI/STA identities matched after a real cold boot.
- Results delivered and A1200 returned to Claude at23:10UTC for driver testing.

Exact runs, including acquisition failures and recovery, are retained in
[hardware results](HARDWARE-TESTING.md). All source/build/release identities are
recorded in each release's metadata.

## Future extensions

- Running-primary DOWNLOAD programming as an alternative to host programming.
- Explicit restoration and damaged-CIS recovery commands.
- Further physical-card coverage and capture-throughput improvements.
