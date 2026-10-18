# Owned before/after operation transaction

## Implemented checkpoint

`include/operation.h` and `src/operation.cpp` implement the session ordering in
the host model. `Prism2GenesisSim inspect|probe` accepts appended
`--output NEW_DIRECTORY --utc YYYYMMDDTHHMMSSZ` options. Without them the host
simulator still runs the original protocol-only checks. Both paths are models.

The native build excludes this transaction library. Native `inspect`/`probe`,
including commands with transaction options, still return 20 before opening the
slot. No CLI flag, saved manifest, candidate profile or old capture enables them.

The sole file journal uses `protocol-model-nv-linear-8m-v1`: a synthetic device
whose complete NV storage is the linear 8 MiB selector-1 interval. Its CIS tuple
is 0274:1613. This profile is explicitly unrelated to approval of the actual
MA401. All its manifests say `simulation_only: true` and
`hardware_authorization: false`.

## Session sequence

1. Validate the supplied timestamp and a new timestamped output directory.
   Open all raw/manifest files before claiming the slot. Existing directories
   are refused, including a previously successful operation's directory.
2. Claim once and configure the non-reset acquisition path. Never call regular
   configuration, release/reclaim, initialize or reset before the backup gate.
3. Acquire two full before captures with bounded consecutive whole-page reads.
   Compare passes, check AUX cleanup, flush/close, reopen and verify both exact
   lengths and SHA-256 hashes. Save, close and reopen-verify the before manifest.
4. Check sticky removal and cancellation again after filesystem verification.
   Only then call the operation, retaining the same exclusive slot claim.
5. After the operation, including failure, acquire two full after captures if
   the insertion remains accessible. Mask Ctrl-C for this evidence readback;
   preserve the operation's cancellation error and never mask removal.
6. Verify saved after files/manifest, recheck all four raw files against their
   acquisition hashes and compare every before/after byte. RAM operations
   require unchanged NV data. An unexpected difference is a failure.
7. Finish non-reset acquisition configuration, record separate phase errors,
   close/reopen-verify the transaction record, then release ownership. There is
   no regular backend reset after the final readback.

The backend's `finish_backup` may restore only a COR value temporarily selected
for acquisition; AUX selector cleanup is performed by each capture. The model
does not prove actual Amiga cleanup behavior. Never replace that exit path with
the regular configure/close path, which resets a native card.

## Evidence and errors

Each new directory contains `before-read-1.bin`, `before-read-2.bin`,
`after-read-1.bin`, `after-read-2.bin`, `before.json`, `after.json` and
`transaction.json`. The parent directory contains the supplied UTC string;
before/after names separate phases. Partial/empty files remain on failures.

Reports retain preparation, acquisition, operation, readback, comparison,
backend cleanup and journal errors separately. A failed final journal write or
verification makes the command fail, even if all earlier phases passed. Treat
the record as incomplete on a journal error; a file's bare success flag is never
authorization. There is no restore/rewrite retry on a failed comparison.

The journal records the operator-supplied UTC string and wrapping model
microsecond start/end values. It explicitly labels UTC as unverified and does
not pretend monotonic times are UTC. These timestamps are sufficient to exercise
model ordering, not the native hardware contract. Filesystem flush/close cannot
promise preservation across power loss.

## Verification

- Unit phase faults cover preparation, claim/configuration, before capture,
  removal/cancellation during the gate, failed/cancelled/removed work, after
  capture, comparison, backend cleanup and final journal failure. Check that
  pre-operation failures cause no command/payload write and only one claim.
- Filesystem tests independently hash all four full captures for modeled
  inspect/x8/x16 probes, verify phase records and reject stale-directory reuse.
  A real `RLIMIT_FSIZE` short-write failure proves storage failure prevents work.
  Injected modeled NV corruption after a Genesis return produces individually
  verified after captures but fails the complete before/after comparison; no
  automatic restore hides the change.
- Native refusal tests use an aborting backend to prove neither plain commands
  nor transaction options reach it. Emulator smoke tests cover those options.

## Actual MA401 candidate and remaining work

The [candidate](evidence/ma401-pd5lmwp200-candidate.json) binds the actual label
and previously observed card tuple to the correct FCC evidence. The offline
`tools/audit_nv_capture.py FIRST SECOND` checks two existing 8 MiB files without
card access, repair or voting. It requires identical raw files and 64 identical
128 KiB windows; validates PDA record bounds, duplicates and CRC-16/ARC; and
checks NIC800c/0/1/0, PDR0400=0003 and absence of PDR0005. It emits hashes and
configuration observations, not MAC/serial/calibration or source paths.

Passing this audit corroborates the candidate observations. It does not prove
physical decode, SRAM part/capacity, separate acquisitions or backup freshness.
The verified existing pair passes with its original full/canonical-window hashes.

The project instructions in `AGENTS.md` and the hardware contract require
independent physical-map and operation-gate verification before native enablement.
Remaining work includes physical decode/hidden-bank review, SRAM corroboration,
native transaction/profile binding, clock validation, persistent-volume resolution
(including assigns rather than only literal RAM:/T: spelling), source commit
provenance and independent gate review. Then run a fresh full before/after
transaction on the reserved A1200 and require unchanged flash for Genesis.
No hardware operation was performed to develop or audit this checkpoint.
