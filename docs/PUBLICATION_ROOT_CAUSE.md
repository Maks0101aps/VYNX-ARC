# Final ZIP publication root-cause evidence

Recorded 2026-10-03. **B — remains blocked. Root cause: STILL UNKNOWN.**
Production change in this pass: **NONE**. No retry policy, publication API switch,
version bump, security-setting change or Pylance-specific product message.

## Source and artifact identity

Actual starting and final Git HEAD on `main`:
`9e759c0517f7979530ae2d14008163333671fd08`.
Previous pass baseline/checkpoint: `220fbff9e3cc7ddaa78b3ea8cd1cba17da370b44`,
retained by `checkpoint/pre-020-20261003`. The previous pass was committed in
`9e759c0`; its old "uncommitted" statement was incorrect. History is unchanged.
This subsequent pass changes diagnostic scripts and documentation in the working
tree. The tested production executable and package are unchanged.

Portable SHA-256:
`129ff5382aa3f4ca4849d045c9158a802a5e31db9f2b948ef4b9d723d70d3105`.
Packaged CLI SHA-256:
`a7df0bd4fb7a20e7e241aeb7e224af1629f1f4015b9068d38891b8feee3f9823`.
Local evidence root: `.dev/030-publication-root-cause/`.

## Pylance ON and remaining OFF control

ON process snapshots: `on1-processes-before.json`, `on1-processes-after.json`.
Pylance was Code.exe PID **17516**, command line containing
`ms-python.vscode-pylance-2026.4.1/dist/server.bundle.js`, client PID 18832.
The same command line is preserved in ProcMon's XML process list.
Fixture workspace:
`C:\Users\Maksi\Documents\projects\VYNX-ARC\.dev\zip-stress-controlled`.

The harness now supports reusing that exact workspace while saving each run to a
new evidence directory. Each category starts with an independent fixture. A
failed publication is never retried; the next cycle proceeds from the last
verified contents. Mixed cycles stop at their first failed operation, and the
next cycle uses the actual remaining original/renamed entry. Successful
publications are checked independently with Python's ZIP reader. Every failed
operation records its immediate before/after archive SHA-256 and verifies the
manifest, entry bytes and MOTW. Unexpected verification errors stop the category.

ON command:

```powershell
python scripts/stress-zip-publication.py --location project --workspace .dev/zip-stress-controlled --evidence .dev/030-publication-root-cause/on1 --continue-on-failure
```

Every row attempted **100 cycles**. Counts below are successful publications /
attempted publications; mixed completed counts mean all four operations succeeded.

| MOTW | Category | Successful publications / attempts | Complete cycles / 100 | Failed publications |
|---|---|---:|---:|---:|
| absent | add | 46 / 100 | 46 | 54 |
| absent | delete | 55 / 100 | 55 | 45 |
| absent | rename | 30 / 100 | 30 | 70 |
| absent | mixed | 178 / 234 | 44 | 56 |
| present | add | 49 / 100 | 49 | 51 |
| present | delete | 53 / 100 | 53 | 47 |
| present | rename | 28 / 100 | 28 | 72 |
| present | mixed | 13 / 110 | 3 | 97 |

Total: **452 successful, 492 failed publications across 944 attempts**.
All **492 failures** preserved the immediate pre-operation archive SHA-256,
manifest and entry bytes, plus the expected zone stream in MOTW cases.
Evidence: `on1/results.json` and `on1/commands.jsonl`.

**OFF: NOT RUN.** Normal editor-close requests interrupted the interactive
session; Code restarted with different PIDs. Those intervals are not absence
controls. The user subsequently selected **"Keep VS Code open for now"**.
No forced termination was performed and no OFF result is claimed. Consequently,
the repeated A/B requirement remains unmet.

The prepared detached runner can perform a future authorized OFF control without
forcing unsaved editors closed. It confirms zero Code/Pylance processes before
running and snapshots processes afterward, retaining its exit/result logs:

```powershell
# Run only when the user is ready to keep VS Code closed for the workload.
Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File scripts/control-publication-off.ps1 -EvidenceName off1 -ReopenCode'
```

It reuses the exact ON fixture path and package. Any OFF run with a restarted Code
process is invalid. If OFF passes, repeat valid ON/OFF pairs at least twice before
assigning causality. To match ProcMon instrumentation, capture both workloads or
run both without it; the current ON capture covered only part of the matrix.

## ProcMon native facts

The Microsoft-signed Process Monitor 4.11 binary was elevated after the user
accepted UAC. Native capture: `on1.pml` (2,605,558,089 bytes).
Full exported CSV: `on1-all.csv`; workspace-filtered CSV: `on1-filtered.csv`.
The capture contains **395 ACCESS DENIED events** for replacement rename
operations. It ended before the last stress failures; the stress log, rather
than the capture count, establishes the full 492 failures.
No security or indexing settings were changed. The original ProcMon filter
bytes were saved and restored after filtered XML export.

Pylance PID 17516 successfully opened exact target ZIP paths 1,475 times:

| Desired Access | Disposition | Options | Attributes | ShareMode | Count |
|---|---|---|---|---|---:|
| Read Attributes | Open | Open Reparse Point | n/a | Read, Write, Delete | 1,141 |
| Read Attributes, Synchronize | Open | Synchronous IO Non-Alert | N | **None** | **320** |
| Generic Read | Open | Synchronous IO Non-Alert | N | Read, Write, Delete | 14 |

Thus the attribute opens with ShareMode None **omit FILE_SHARE_DELETE**. The
other two classes explicitly include it. This is observed CreateFile detail,
not an inference from handle existence. All these events report **SUCCESS**.
`procmon-events.json` preserves the exact opens and denied rows.

Example external open:

```text
12:16:46,4263846
Code.exe, PID 17516, CreateFile, SUCCESS
...\.dev\zip-stress-controlled\plain\add.zip
Desired Access: Read Attributes, Synchronize
Disposition: Open
Options: Synchronous IO Non-Alert
Attributes: N
ShareMode: None
```

The exact exported time is **12:16:46,4263846**. It is followed by a CloseFile at
12:16:46,4265266. The later denied rename is at 12:16:46,4660160; this particular
short-lived attribute open therefore does not alone establish handle overlap
with that denied event. Other external reads are present in the lifecycle.
Do not claim every denial coincided with one of these share-zero opens.

The first denied native event:

```text
Time: 12:16:46,4660160
Process: vynxarc-cli.exe, PID 21540
Operation: SetRenameInformationFile
Path: ...\plain\.tmpqg2n05
Result: ACCESS DENIED
Detail: ReplaceIfExists: True, FileName: ...\plain\add.zip
```

Its preceding SetBasicInformationFile setting the temporary file to normal
attributes succeeded at 12:16:46,4650957. The stack includes:
`vynxarc-cli.exe → KernelBase!MoveFileExW + 0x1d →
MoveFileWithProgressTransactedW + 0x32f → ntdll!ZwSetInformationFile + 0x14 →
ntoskrnl!NtSetInformationFile → FLTMGR`.
The CLI reports Win32 **ERROR_ACCESS_DENIED (5)**. This is not an observed
ERROR_SHARING_VIOLATION (32). FLTMGR appearing in the stack does not identify a
particular minifilter as the cause.

Full symbolized exports: `first-denied-stack.xml` and
`pylance-opens-stack.xml`; compact extracted events:
`first-denied-event.xml`, `pylance-no-share-event.xml`.
The external share-zero open's stack includes `CreateFileW`, `uv_fs_stat` and
`uv_fs_realpath`. Filter configurations used only for offline export are retained
as `first-denied.pmc` and `pylance-opens.pmc`.
`first-denied-timeline.json` preserves surrounding opens, closes, attribute and
rename events. ProcMon's [native logging and stack capture](https://learn.microsoft.com/en-us/sysinternals/downloads/procmon)
support inspection of this evidence independently.

## Controlled handle-sufficiency probe

`scripts/probe-publication-share-mode.py` holds a known target CreateFile handle
through a single packaged rename, using independent unheld archive copies as
controls in TEMP. This is diagnostic evidence, not a production retry or API change.
`share-mode-probe.json` and its retained TEMP workspace report:

| Held desired access | Held share mode | Held rename | Independent unheld rename |
|---|---|---|---|
| Read Attributes + Synchronize (`0x100080`) | None (`0`) | error 5 | success |
| Generic Read (`0x80000000`) | Read + Write + Delete (`7`) | error 5 | success |
| Generic Read (`0x80000000`) | Read + Write (`3`) | error 5 | success |

All three failed archives kept identical before/after SHA-256 and original bytes.
This demonstrates that a held handle can reproduce the native error on this
machine and that **delete sharing alone is not sufficient for this replacement
primitive to succeed**. It does not prove that Pylance caused the field failures,
prove transience, or distinguish a lock-related error 5 from ACL/read-only error 5.
Therefore no error-5 retry classification is justified by these probes alone.

## TEMP control and quality gates

Fresh TEMP workspace: `C:\Users\Maksi\AppData\Local\Temp\zip-stress-g1nlsv83`
(the authoritative exact workspace is in `temp1/results.json`).
`temp1/results.json` and `temp1/commands.jsonl` retain 100/100 add, delete, rename
and mixed cycles in both MOTW states: **1,400/1,400 successful publications**,
zero failures, independently verified bytes/manifests and zone streams.

Fresh `scripts/check.ps1` completed with exit 0; `final-check.log` retains:
format check, strict Clippy, **50 Rust tests**, **3/3 CTest targets**, and cargo
audit (1,290 advisories, 107 dependency crates, no vulnerabilities reported).
The known unused global `vswhom-sys` Cargo patch notice remains an environment
notice, not a Clippy diagnostic. Diagnostic Python syntax, PowerShell parse and
Git whitespace checks also pass.

Acceptance remains blocked by the failing `.dev` matrix and absent repeated OFF
control. The controlled handle probe does not replace that acceptance requirement.

## Subsequent causal-control request: OFF state not available

On 2026-10-03, the user requested two fresh OFF matrices with the same packaged
binary. HEAD and the portable/CLI hashes above were reverified unchanged.
The new detached runner, `scripts/run-publication-causal-controls.ps1`, waits for
zero Code/extension-host/Pylance/pyright processes before any archive test. It
would run the full original add/delete/rename/mixed matrix in two fresh `.dev`
workspaces, with before/after full process snapshots, one-second server-process
samples and a Code process-start event watcher. The stress harness now also logs
successful before/after SHA-256 values and independent contents verification for
every publication in `publication-checks.jsonl`.

The initial observation still contained Code.exe and Pylance PID 16380. The user
then selected **"Cannot close VS Code yet"**. Only the waiting helper was stopped;
no Code process or unrelated Windows process was terminated. **No archive test
started**, no OFF workspace was populated, and no optional ON-again run was made.

| Requested control | Result |
|---|---|
| OFF #1: 100 rename + 100 mixed cycles per MOTW state | NOT RUN — true OFF state unavailable |
| OFF #2: fresh workspace, same matrix | NOT RUN — true OFF state unavailable |
| ON again after OFF | NOT RUN |

Local evidence:
`.dev/040-pylance-control-20261003T154220Z/identity.json`,
`wait-for-off.jsonl`, `processes-deferred-all.json`,
`processes-deferred-related.json` and `status.json`.
The earlier waiting-helper attempt at
`.dev/040-pylance-control-20261003T153913Z/` stopped on an evidence-log file-sharing
error before any archive test; it is not an OFF result. The logger was changed to
hold its own log open, and the subsequent helper retained its wait observations.
Python syntax, PowerShell parsing and Git whitespace checks passed. Production
sources are unchanged; no new archive-runtime validation is claimed in this
continuation.

Root cause stays **STILL UNKNOWN**. New AccessDenied/archive-preservation results:
**not measured**, because neither OFF workload started. The previous ON capture
and preservation evidence remain historical measurements, not substitute controls.
[Proposed production handling](PUBLICATION_HANDLING_DESIGN.md) is design only:
qualified error 32/33 conflicts may receive bounded waiting; error 5 must fail by
default without a reliable runtime classification. No implementation was made.
