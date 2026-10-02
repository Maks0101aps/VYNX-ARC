# Implementation status вЂ” 0.1.0 development milestone

## Work toward 0.2.0 (in progress)

The recoverable 0.1.0 baseline is Git commit `4303895`; MSVC migration checkpoint
`4ef433d` is pushed to main. Qt/CXX architecture and working archive support remain.
MSVC builds and deployed GUI/CLI checks passed with developer paths removed.
The GNU directive warning is explained in TOOLCHAIN.md and no longer emitted by
the MSVC build. ZIP/7Z streaming modification, GUI commands / drag-to-add, explicit
creation format and free-space preflight have passed core and deployed checks.
Extraction conflicts now support Ask, Replace, Skip, Keep both, Newer and Stop,
with comparison details and apply-all. See CONFLICTS.md for semantics and limits.
Entry hashing and supplied-digest verification are implemented (HASHING.md).
Verification reports now include match status, algorithm and actual/expected values.
The native Explorer component and sparse identity package are implemented, with
COM/IPC tests. Signing and actual Windows menu activation remain pending; see
WINDOWS_INTEGRATION.md and CLEAN_VM_CHECKLIST.md. No trust settings were changed.
Split-7Z reading/creation, exact missing-volume messages and real RAR4/RAR5
multipart decoding are implemented; see VOLUMES.md for limits and known issues.
RAR fixture binaries stay local and are restored from a pinned upstream commit
with SHA-256 checks before CI tests.
Current source validation: 44 Rust tests and 3 CTest targets (seven presentation
checks, native shell smoke and GUI smoke including the actual conflict dialog
and entry hash bridge). The counts below are historical.
Password-copy reduction and boundary audit are documented in PASSWORD_LIFETIME.md;
Qt and cryptographic backend wiping limitations remain explicit release gates.
See MODIFICATION.md for the transaction model and metadata-preservation limitations.
This is not a claim that the new 0.2.0 prompt or production-readiness gates are complete.

### Native UI refactor and visual QA

Home and archive workspace are separate pages. Navigation, breadcrumbs, search,
shared command actions, numeric/date sorting, cached vector icons, local recents,
operation progress/ETA and creation/extraction/conflict/settings/about dialogs
are implemented as focused presentation components. MainWindow keeps worker and
archive ownership. Existing core security policies are unchanged; timestamp
metadata is exposed through the bridge for display/sorting only.

See UI_DESIGN.md and UI_QA.md for the visual matrix, reproducible commands,
regression evidence, known limits and before/after RAM measurements. This is a
development UI milestone, not production or complete 0.2.0 acceptance.
On portable artifact SHA-256 `7fc7a01842c06742aa668216fee2d58efbb13dba3f5c0470277406dec2ea828f`, the packaged verifier produced a reproducible location matrix. With both app and archive workspace under `.dev`, all three attempts failed at ZIP rename with Win32 error 5. With the app under `%TEMP%` and archive workspace under `.dev`, all three failed. With the app under `.dev` and archive workspace under `%TEMP%`, all three passed; both under `%TEMP%` also passed 3/3. A `.dev` `NoIndex` probe still failed 3/3. Simple packaged ZIP-add passed 5/5 in each location, and direct `MoveFileExW` replacement passed 10/10 in both. Thus the archive workspace location is associated with the verifier failure; the responsible process/filter or policy is not identified. Defender real-time was enabled, Controlled Folder Access was disabled, and no matching block events were found. Windows `tempfile::persist` calls `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`; code 5 is `ERROR_ACCESS_DENIED`. This evidence does not establish an application-code root cause or clear the normal Documents/project location. Release publication acceptance remains blocked. The verifier records both execution paths; `VERIFICATION_LAST_SUCCESS.json` remains historical.
Native RAR code 15 was traced to the vendored UnRAR DLL's process-global mutable
`ErrHandler`, which is reset on each open; overlapping calls can interfere. The
adapter now serializes decoder lifetimes. Its concurrent regression repeats open,
list and test 10 times across RAR4, RAR5, encrypted and multipart fixtures. The
prior parallel test failure and repeated passing runs support this fix; broad
external-process concurrency is not claimed.

### Remaining 0.2.0 acceptance work

- Implement format-specific compression presets and advanced encoder settings;
  only split-size selection is currently exposed in the advanced creation UI.
- Implement Eco/Balanced/Maximum resource policies with effective codec settings,
  bounded metadata allocations before parsing, and measured memory behavior.
- Broaden ETA/phase-total acceptance on real long operations and review throttling.
- Broaden malformed-input, decompression-bomb, Unicode/collision and filesystem-race
  coverage; audit remaining Qt/cryptographic-backend password copies and lifetimes.
- Diagnose the location-associated ZIP replacement `AccessDenied` in the Documents
  project workspace. Three-trial controls pass when only the archive workspace moves
  to `%TEMP%`, but this does not clear the normal project path or identify the
  external process/filter. Ancestor pin release and a `NoIndex` probe were not causes.
- Extend performance validation with cache-controlled runs, repeated open/close
  memory-growth tests and external 7-Zip/WinRAR comparisons. Three-trial dataset
  medians are recorded in `PERFORMANCE.md`; one-trial 100k browsing and the absence
  of cache control remain limitations.
- Verify signed Explorer activation, actual selection/menu behavior, and clean
  Windows 11 install/uninstall. No certificate is available; MSIX stays unsigned
  and unregistered. CLEAN_VM_CHECKLIST.md is prepared but has not been executed.
- Review metadata preservation and complete final independent package verification
  before claiming the 0.2.0 milestone complete. BLAKE3 remains optional.

The following sections retain the original 0.1.0 evidence for comparison.

Evidence recorded on 2026-10-02 on Windows 11 Pro x64. This is an implementation
session report, not V1 acceptance or a production security certification.

## Implemented

- Rust archive operations and CXX bridge; native Qt Widgets GUI and standalone CLI.
- ZIP / 7Z / RAR4 / RAR5 / TAR / TAR.GZ browse, test and extract.
- ZIP / 7Z / TAR / TAR.GZ creation, staging and round-trip verification.
- ZIP AES-256; 7Z data/header encryption; encrypted RAR decoding.
- Selected extraction, Extract Here, named destination and Smart Extract.
- Refuse / skip / replace conflicts; default preservation of existing files.
- Windows path, collision, link, reparse, declared/actual size checks; temporary
  file cleanup; destination ancestor handles deny renaming while publication runs.
- Mark-of-the-Web propagation, SHA-256 / CRC32 file hashing.
- Model/view table, folder navigation, filename/wildcard filter, sorting,
  column visibility, multi-selection, keyboard shortcuts, drag-to-open.
- Background worker, progress, current path, throughput, elapsed time, cancellation.
- System / light / dark appearance, English / Ukrainian / Russian UI, local recent
  archives with clear/disable controls and portable settings.
- Native Windows frame and DPI manifest, original icon, portable deployment,
  per-user Inno installer with optional Open With and shortcuts.
- Pinned dependency versions, vendored official UnRAR 7.23, license collection,
  documentation, local build scripts and a CI workflow.

## Tested

- **21 Rust tests passed:** 7 unit, 3 real RAR fixture, 9 archive/security
  integration and 2 Windows filesystem tests.
- **1 Qt/CXX GUI smoke test passed:** asynchronous open, Unicode folder navigation,
  filter/selection, create/test/extract byte comparison, 100,000 metadata rows,
  cancellation error translation and explicit light/dark palette checks.
- ZIP and 7Z output read by independent Python `zipfile` and `py7zr` readers.
  This exposed and fixed missing 7Z directory attributes.
- Packaged CLI round trips for all four writable formats and real RAR fixture tests.
- Packaged GUI and CLI run with PATH restricted to Windows/System32 and developer
  Qt environment variables removed. This is environment isolation on the build
  machine, not a fresh Windows VM.
- Real QWidget screenshots reviewed at 100%, 125% and 200% scaling in three
  languages. Screenshot review exposed and fixed light mode inheriting a dark
  system palette. Screenshots do not prove native title-bar / Snap behavior.
- Cargo fmt, Clippy with `-D warnings`, and cargo-audit passed locally. Audit
  findings cover known RustSec advisories only.
- Portable ZIP and Inno installer built; checksums generated. CI itself has not run.

See VERIFICATION.json for packaged checks and PERFORMANCE.md for bounded measurements.

## Remaining

- Transactional archive add/delete/rename and drag-in/drag-out workflows.
- Standalone compression formats and remaining CAB/ISO/WIM/CPIO/AR families;
  split-volume acceptance and actionable missing-volume messages.
- Modern Explorer IExplorerCommand integration and optional legacy context menu.
- Compression presets, resource policy, operation queue, ETA and detailed conflict
  decisions (ask, rename, date/newer comparisons).
- Entry hashing and supplied-hash verification; free-space preflight.
- Full parser memory budgets, comprehensive password-copy zeroization, fuzzing,
  broader adversarial filesystem races and malformed-input regression corpus.
- Clean Windows VM installer/uninstaller checks, broader accessibility/keyboard/
  native frame QA, cold-start/idle/large-archive benchmarks, ARM64/MSVC validation.
- Authenticode signing, MSIX if appropriate, reproducible public release and update
  policy. No network update checker exists.

## Known issues

Link-containing archives are rejected as a whole. Completed files remain after
extraction cancellation; extraction is transactional per file, not per directory.
TAR metadata browsing scans the stream; solid compression can decode unselected
entries. Backend diagnostics remain English. Qt/backend password copies are not
all guaranteed to be wiped. The creation format is determined by the output path;
the user must keep its extension consistent with the selected format.

The Rust GNU / MinGW linker emits a `corrupt .drectve at end of def file` warning;
the resulting executables passed local and packaged tests. Packages are unsigned.
Installer installation/uninstallation and hostile local-process race resistance
are unverified. Do not advertise this milestone as production ready.

## Build command and runnable artifacts

```powershell
scripts/build-release.ps1
```

`dist/VYNX-ARC-Portable-x64.zip`, `dist/VYNX-ARC-Setup-x64.exe`,
`dist/SHA256SUMS.txt`. Open the extracted `VynxArc.exe` to run the GUI.
