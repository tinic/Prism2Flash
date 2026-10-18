# Owned before/after operation transaction

## Current native and model paths

`include/operation.h` and `src/operation_runner.cpp` share the owned session
ordering. `src/operation.cpp` implements persistent model/native journals.
Native admission permits the qualified MA401 x16 probe/bootstrap, validated SST
programming and content/RID verification paths. Standalone legacy inspection,
x8 probing and other cards remain refused. See [native commands](NATIVE-FLASH.md).

The host-only `protocol-model-nv-linear-8m-v1` factory remains separate from
native admission. Its synthetic CIS is 0274:1613; a model manifest or saved file
cannot authorize hardware. `simulation_only` distinguishes those records;
`hardware_authorization: false` means a persisted report is not an authorization
capability, including in actual native records.

## Session sequence

1. Validate the supplied timestamp and a new timestamped output directory.
   Open all raw/manifest files before claiming the slot, reserving the final
   transaction record first. If a subsequent output open fails, close partial
   output streams and attempt the failure record without claiming the slot.
   A failure to create the directory or first record cannot leave a saved record;
   report the command failure and retain whatever evidence exists. Existing directories
   are refused, including a previously successful operation's directory.
2. Claim once and configure the non-reset acquisition path. Never call regular
   configuration, release/reclaim, initialize or reset before the backup gate.
3. Native: acquire one full before capture, checking every 128 KiB alias against
   the first physical image. The host model retains two full captures. Use
   bounded consecutive whole-page reads in both paths.
   Compare passes, check AUX cleanup, flush/close, reopen and verify both exact
   lengths and SHA-256 hashes. Save, close and reopen-verify the before manifest.
4. Check sticky removal and cancellation again after filesystem verification.
   Only then call the operation, retaining the same exclusive slot claim.
5. After the operation, including failure, acquire one full native after capture
   (two in the host model) if
   the insertion remains accessible. Mask Ctrl-C for this evidence readback;
   preserve the operation's cancellation error and never mask removal.
6. Verify saved after files/manifest, recheck every raw file against its
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

Each native directory contains `before-read-1.bin`, `after-read-1.bin`,
`before.json`, `after.json` and
`transaction.json`. The parent directory contains the supplied UTC string;
before/after names separate phases. Partial/empty files remain on failures.

Reports retain preparation, acquisition, operation, readback, comparison,
backend cleanup and journal errors separately. `finish()` may be called after
failed `prepare()`, without any slot ever being opened; journals must support
that early failure path. A failed final journal write or
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
  Real `RLIMIT_FSIZE` short-write and `RLIMIT_NOFILE` open failures prove
  storage failure prevents work. The latter must leave a failed transaction
  record when its initial stream could be reserved. A decorator around the
  real file journal tampers independently with each saved raw file after phase
  verification and requires `P2_E_VERIFY` during final comparison.
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
128 KiB windows; walks the CIS chain at window offset 0xfe00 and checks
MANFID000b:7300; corroborates PK010007.HEX/SF010306.HEX at their observed
offsets; validates PDA record bounds, 512-byte length limits, duplicates and
CRC-16/ARC; and checks NIC800c/0/1/0, PDR0400=0003 and absence of PDR0005. It emits hashes and
configuration observations, not MAC/serial/calibration or source paths.

Passing this audit corroborates the candidate observations. It does not prove
physical decode, SRAM part/capacity, separate acquisitions or backup freshness.
The verified existing pair passes with its original full/canonical-window hashes.

The project instructions in `AGENTS.md` and the hardware contract require
independent physical-map and operation-gate verification before native enablement.
Remaining work includes physical decode/hidden-bank review, SRAM corroboration,
retaining resolved destination ownership across file opens, a native physical-map
journal with best-effort date labels, profile binding and independent
gate review. Source revision is now recorded in model manifests. Native preflight
volume diagnostics are implemented below, but are snapshots rather than
transaction capabilities. Then run a fresh full before/after
transaction on the reserved A1200 and require unchanged flash for Genesis.
No hardware operation was performed to develop or audit this checkpoint.

## Native diagnostic preflight (0.8)

```
Prism2Genesis preflight --output Work:Prism2-20261018T120000Z --utc 20261018T120000Z --utc-offset -420
```

The UTC/offset arguments in this legacy diagnostic command are optional label
metadata, not evidence of a correctly set clock. Native card operations do not
check clock validity, skew, jumps or agreement with a reference. The current
flashing commands need neither argument; use [the quick start](QUICKSTART.md).

The destination's existing parent is resolved with `Lock`/`NameFromLock`;
`Info` and `SameLock` check the actual volume, including through assigns. RAM
and its aliases, unknown filesystem types, write-protected/validating disks,
invalid disk statistics and insufficient space are refused. The conservative
filesystem allowlist is AmigaDOS DOS0–DOS8. Other filesystems require review
before inclusion; their rejection is not a claim that they are volatile.
Space must cover two 8 MiB capture files, intended-image buffers and journal allowance.
The leaf must be new and consist of letters, digits, hyphens or underscores.

The experimental `backup` command also uses the resolved-volume check before
creating its directory; an assign to RAM no longer passes the old literal-name
check. This acquisition still does not authorize an impactful operation.

The preflight command creates no directory/files and does not open card.resource. Its
locks are released on return. It cannot authorize a later command: storage and insertion ownership must be reacquired/retained in the eventual native
journal. Native active commands, including appended transaction options, continue
to refuse before opening the slot. No user-editable profile or approval flag is
accepted. The C++ MA401 observation validator also returns no hardware authority.

DeepSeek P2R9 F1–F4 are addressed by the audit/assertion/failure paths above.
Optional F5 firmware anchors are corroboration only; their alignment and one
complete alias period do not prove fitted die capacity, NVds/ROMds overlay,
write/erase decode or SRAM capacity. No physical-profile gate is closed by them.
