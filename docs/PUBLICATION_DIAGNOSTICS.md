# Windows ZIP publication investigation

2026-10-03: the error-5 blocker remains unresolved. Do not infer acceptance from
TEMP successes, retries, or a passing diagnostic run. Normal publication now
makes one attempt; security software, indexing and MOTW remain enabled.

## Reproduce and bisect

From the repository root after building the real package:

```powershell
python scripts/reproduce-zip-publication.py --location project --sequence full --runs 5
python scripts/reproduce-zip-publication.py --location temp --sequence full --runs 5
python scripts/reproduce-zip-publication.py --location project --sequence all --runs 5
python scripts/reproduce-zip-publication.py --location project --sequence full --full-prefix 7z --runs 5
python scripts/stress-zip-publication.py --location project
python scripts/stress-zip-publication.py --location temp
```

Each invocation creates its own workspace and retains `commands.jsonl` and
`results.json`, with exact commands, timestamps, paths, executable hash and
failures. No failed operation is retried. The full prefix performs deployed GUI
smoke, ZIP creation/test/list/independent read/extract/Smart Extract, other writable
format round trips with independent 7Z extraction, and repeated RAR checks before
the same ZIP rename used by `verify-portable.py`. `--full-prefix` stops that prefix
at ZIP, 7Z, TAR, TAR.GZ or RAR. Standalone controls cover list, test, extract, hash,
Smart Extract, add/delete, RAR and GUI before rename.
`--last-format-stage` further bisects the last format into create/test/list/extract/
Smart Extract. The smallest tested prefix with an observed failure is ZIP and 7Z
round trips followed by TAR create/test (1 failure in 5); TAR create alone passed
5/5. This is an intermittent sequence/timing boundary, not proof that TAR causes
the ZIP denial. The complete prefix failed 5/5 in the final package matrix.
Stress categories use separate
archives so an add failure cannot prevent deletion/rename testing. Delete fixtures
are independently seeded; published bytes/manifests are checked after each operation.

## Diagnostic build and live Windows APIs

```powershell
. scripts/env.ps1
cargo build --workspace --release --locked --features vynx-arc-core/publication-diagnostics --target-dir .dev/publication-diagnostic-target
python scripts/stress-zip-publication.py --location project --diagnostics --cli .dev/publication-diagnostic-target/release/vynxarc-cli.exe
```

The feature is off in normal packages. Stage logs cover SOURCE_LOCKED,
REBUILD_STARTED/FINISHED, TEMP_SYNCED, VERIFY_OPEN/FINISHED/DROPPED,
METADATA_HANDLES_DROPPED, SOURCE_GUARD_DROPPED, FINAL_ANCESTOR_CHECK and
PUBLISH_BEGIN/FAIL/SUCCESS. Synchronous source streaming and metadata readers
complete before publication. `Archive` retains metadata, not open file handles.
Passwords and zone contents are never logged.

For a snapshot immediately before publication, set `VYNX_DIAGNOSTIC_PYTHON` to
the absolute Python executable and `VYNX_DIAGNOSTIC_HELPER` to the absolute
`scripts/diagnose-publication.py` path. Leave `VYNX_DIAGNOSTIC_ON_FAILURE_ONLY`
unset. Stress/reproducer diagnostic runs set it to avoid a pre-publication helper
delay masking the race; failure snapshots occur while the failed CLI remains alive.

The helper enumerates actual kernel handles through
`NtQuerySystemInformation(SystemExtendedHandleInformation)` and `DuplicateHandle`,
resolves file paths and queries volume/file identities. It records inaccessible
handles explicitly. In captured failures, file-object handles were inspectable;
unsupported duplications belonged to a different object type. It also queries
mapped files through VirtualQueryEx/GetMappedFileNameW. These snapshots do not
capture every transient handle between events and do not expose share flags.
DELETE-access opens with all sharing enabled provide a separate instantaneous
sharing probe, not proof of historical sharing or absence of filter interference.

Restart Manager uses RmStartSession/RmRegisterResources/RmGetList/RmEndSession for
the source and temporary sibling, recording names, PIDs, application types, empty
results and errors. External users it reports receive separate handle/mapping
snapshots. No process is terminated, suspended or reconfigured.

The failure-time evidence identifies Code.exe PID 26548 using the archive. Its
command line belongs to `ms-python.vscode-pylance-2026.4.1/dist/server.bundle.js`.
An external kernel snapshot caught its read handle to the exact ZIP. DELETE-access
probes nevertheless succeeded and no matching mapped region was observed. Thus
Pylance is an evidenced concurrent user, not yet a proven cause of the denial.

## MOTW controls and environment comparison

Run the reproducer with `--motw absent`, `--motw present`, or `--motw independent`.
The independent mode writes ADS bytes from a separately staged zone file.
Only a diagnostic-feature CLI supports `--diagnostics --skip-zone --motw present`;
the helper log explicitly records skipped propagation. This must never be used
as an acceptance result or shipped as a MOTW workaround.

```powershell
scripts/compare-publication-environment.ps1
scripts/test-publication-permissions.ps1
```

The first records volume/filesystem, owner, inherited ACLs/SDDL, integrity,
attributes (including compression, encryption and NoIndex), ancestor reparse/cloud
attributes, path lengths, OneDrive roots and Defender/CFA state without changing
them. The second applies and restores ACL denials only on newly created TEMP
fixtures, verifying the original archive hash after restoration.

## Exact Process Monitor capture procedure

Automatic capture was not available in this medium-integrity, non-elevated
session. The Microsoft-signed binary is at `.dev/020-procmon/Procmon64.exe`.
Use an administrator-authorized session to perform the following manual capture;
do not change Defender, indexing, Developer Mode or certificate trust.

1. Open [Microsoft Process Monitor](https://learn.microsoft.com/en-us/sysinternals/downloads/procmon).
   Stop capture (Ctrl+E), clear old events (Ctrl+X), enable File System Activity,
   and disable Registry/Network/Process profiling categories for this capture.
2. Filter Path **begins with**
   `C:\Users\Maksi\Documents\projects\VYNX-ARC\.dev\zip-publication-` Include.
   This includes both the archive and its random temporary sibling. Do not filter
   by process name: the external actor is part of the investigation. Leave
   "Drop Filtered Events" off until the filter has been checked.
3. Add Operation Include rules for CreateFile, CloseFile,
   SetRenameInformationFile, SetDispositionInformationFile, QueryOpen,
   FileSystemControl and SetBasicInformationFile (the tempfile attribute step).
   Same-column Include rules are OR-ed; Path and Operation filters combine.
4. Start capture (Ctrl+E). In a normal terminal run:
   `python scripts/reproduce-zip-publication.py --location project --sequence full --runs 5`.
   Stop capture immediately after failure. Save native PML with all captured
   events and export the filtered view as CSV alongside that workspace's logs.
5. Narrow Path to the exact failed `Project.zip` OR Path begins with its parent
   followed by `\.tmp`. Add Time of Day, Process Name, PID, Operation, Path,
   Result and Detail columns. Retain successful opens/closes, not just failures.
6. Inspect the denied operation and its stack. Check CreateFile desired access,
   share modes, dispositions, and file/ADS opens between verification completion
   and publication. Follow PID 26548 or the current Restart Manager PID through
   Process Tree to its Pylance command line. Capture any other process/filter
   before assigning causality. Compare with a TEMP capture using the same sequence.

Publication stays on pinned `tempfile` 3.27.0: SetFileAttributesW followed by
MoveFileExW(MOVEFILE_REPLACE_EXISTING). The error does not alone identify which
native call failed. A direct MoveFileEx control omits the attribute step and the
full archive lifecycle. Neither ReplaceFileW nor another primitive has been
substituted to hide the failure. [Microsoft documents extended paths and ACL
requirements for MoveFileExW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw).
