# Native UI QA — 2026-10-02

## Implemented and refactored

Presentation lives in `apps/gui/pages/{HomePage,ArchivePage}`, five files under
`apps/gui/dialogs`, and widgets for breadcrumbs, vector icons, recent rows,
operation progress and toasts. `Theme.cpp` owns the palette; `ArchiveModel.cpp`
owns typed sorting and cached totals. `MainWindow.cpp` retains archive state and
worker orchestration. `UiCapture.cpp` contains explicit screenshot fixtures.

Home keeps Open/Create, identity, drop target and recent archives. The workspace
starts with Back/Up, breadcrumbs/search and a compact shared-action command bar.
The table starts at y=125 instead of baseline y=188 at 100%: **63 logical pixels
recovered**. File double-click opens Properties. The creation format controls the
extension; unsupported compression presets are not simulated. About reports
project/Qt/build metadata, source, licenses and the absence of telemetry.

## Visual evidence

Baseline source is `36451d7`; checkpoint `1c7acaa` preserves the runner. All 48
baseline captures remain in `.dev/ui-baseline`; four representative originals and
the complete one-second sample list remain locally in `screenshots/ui-before`.
Generated image matrices and JSON reports are ignored by Git. The runners recreate
them locally; four curated browser illustrations remain tracked for documentation.

The polished presentation from `79b93ed` produced **126 real QWidget captures**:

| Directory | Coverage | PNGs |
| --- | --- | ---: |
| `screenshots/ui` | Home/archive, light/dark, en/uk/ru, 100/125/150/200%, 1060×720 | 48 |
| `screenshots/ui-details` | Create/Extract/Conflict/Settings/About/progress/breadcrumb, both themes, three languages, 100% | 42 |
| `screenshots/ui-sizes` | Home with recents/archive, 620×440, 800×600, 1440×900, both themes, three languages, 100% | 36 |

Representative images were inspected across both themes, all four scales, all
three languages, each requested window size and each dialog type. Review found
and fixed Ukrainian Ratio header clipping and an overly narrow Extract dialog.
The Ratio width now follows translated font metrics. At narrow widths, secondary
columns and Delete/Rename move out of the command row while shared overflow
actions remain available. Recents render filename/path/icon without row widgets.
The screenshot operation fixture hides the preceding open-completion toast so
that current path, bytes, speed, Cancel and ETA remain visible.

QWidget grabs exclude the native title bar, Snap and OS menus. They are actual
application rendering, not mockups. Progress/conflict metadata are explicit QA
fixtures; they do not measure extraction speed or prove a real conflict. The GUI
smoke separately exercises a real asynchronous conflict and bridge reply.
The final build adds smoke coverage only; it does not change this presentation.

## Current blocker pass (2026-10-03)

Current source validation passes 50 Rust tests and 3 CTest targets. The expanded
RAR concurrent test covers 120 sequences. Project ZIP replacement still fails
with and without MOTW after removing retries. TEMP passes do not clear the project
gate. Failure-time APIs report Pylance using the ZIP; precise causal attribution
still requires the prepared capture. Current results, package hashes and remaining
manual gates are in [RELEASE_ACCEPTANCE.md](RELEASE_ACCEPTANCE.md).

## Functional regression (historical 2026-10-02 checkpoint)

- Final release build: **44 Rust tests** and **3 CTest targets passed**:
  presentation (seven behavior checks), native shell smoke and GUI smoke.
- Presentation checks cover numeric/date ordering and missing dates, folder-first
  order, cached folder totals, breadcrumb mouse/keyboard/collapse behavior, shared
  action availability/read-only controls, live creation format/password behavior,
  cautious ETA resets and vector icons across palettes.
- GUI smoke covers actual drag/drop opening events, asynchronous ZIP opening,
  Unicode navigation/filter/selection, creation/test/extraction bytes, conflict
  dialog Skip/apply-all, rename/reopen/hash bridge, encrypted work/password shared
  lifetime, cancellation, 100,000 metadata rows and Light/Dark/System palettes.
  Synthetic drop events do not establish Explorer drag-out interoperability.
- The Windows release build additionally passed full parallel Rust tests (44 total),
  including the repeated RAR regression, and the same three CTest targets.
- The deployed verifier restricts PATH to Windows/System32 and removes developer
  Qt variables. It independently reads ZIP/7Z, checks ZIP/7Z/TAR/TAR.GZ round trips,
  real RAR fixtures, add/delete/rename/hash/verify and split-7Z/missing parts.
  Smart Extract now checks actual bytes and absence of duplicate root nesting.
  The same package passed 3/3 runs with its archive workspace under `%TEMP%`, and
  failed 3/3 with the archive workspace under `.dev`, regardless of the app extraction
  location. Each attempt completed GUI/CLI smoke, four format round trips, Smart
  Extract and 80 packaged RAR operations. The exact responsible process or filesystem
  filter remains unknown, so packaged modification acceptance is blocked for the
  project/Documents workspace. `VERIFICATION.json` and
  `VERIFICATION_ATTEMPTS.json` retain the latest path-specific run; the last-success
  report is not overwritten.

## Measurements and limitations

`UI_MEMORY.json` records three fresh launches for each revision/state/theme, the
same small ZIP, English, 100% scaling and isolated PATH. Working set is sampled
after five seconds settling and one second CPU observation. A 100ms startup poll
is an observed peak, not an exact maximum. `PERFORMANCE.md` summarizes settled UI medians and three-trial operation medians.
Those samples compare UI working sets; they do not establish leak growth or
large-archive memory bounds. See PERFORMANCE.md for three-trial operation
medians and their limits.

The verifier path matrix reproduced a location-associated replacement failure on
portable SHA-256 `7fc7a01842c06742aa668216fee2d58efbb13dba3f5c0470277406dec2ea828f`.
Runs with the archive workspace under `.dev` failed 3/3 whether the app was
extracted to `.dev` or `%TEMP%`; runs with the workspace under `%TEMP%` passed 3/3
for either app location. A `.dev` `NoIndex` probe still failed. Direct
`MoveFileExW(MOVEFILE_REPLACE_EXISTING)` tests passed 10/10 in both locations, and
simple packaged ZIP-add passed 5/5 in each. The verifier's full sequence therefore
exposes a location-sensitive interaction that those smaller controls do not.
Windows publication uses `MoveFileExW` through `tempfile::persist`; code 5 means
`ERROR_ACCESS_DENIED`, but the responsible process or policy remains unidentified.
Defender real-time protection was on, Controlled Folder Access was off, no matching
block events were found, and post-failure inspection found no app-owned archive
handles. Search indexing is not established as the cause. Ancestor-pin release was
falsified; no acceptance was inferred from relocating the verifier workspace.


RAR code 15 in the bundled UnRAR API is `ERAR_EOPEN`: a starting archive or
required volume failed to open. Inspection found that UnRAR resets and reads its
process-global `ErrHandler` on each `RAROpenArchiveEx`; overlapping decoder calls
could race. A global Rust mutex now covers list and complete stream/test lifetime.
The previous parallel failure is recorded in the local test log; a regression
exercised 50 open/test cycles across RAR4, RAR5, encrypted and multipart fixtures,
and packaged verification performed 80 RAR list/test operations on each run.
Repeated checks passed after the fix. This closes the observed in-process race,
not arbitrary multiple-process or external file access.

Remaining UI acceptance: real screen-reader traversal, native frame/Snap,
Explorer drag-out, clean Windows VM install/uninstall, signed Explorer activation,
high-contrast/very long localized paths and real long-operation ETA acceptance.
Creation uses the actual format compression default; presets remain backend work.
The whole archive page still disables while a worker owns mutable state. Default
ZIP DOS-epoch timestamps show a dash, also hiding legitimate 1980-01-01 dates.
Advanced split controls could use a more explicit disclosure affordance.

## Reproduce and run

```powershell
scripts/build-release.ps1
python scripts/verify-portable.py
python scripts/ui-screenshots.py --app dist/STAGE --output docs/screenshots/ui --workspace-content
python scripts/ui-screenshots.py --app dist/STAGE --output docs/screenshots/ui-details --details-only
python scripts/ui-screenshots.py --app dist/STAGE --output docs/screenshots/ui-sizes/620x440/uk --quick --language uk --window-size 620x440 --workspace-content --recents
python scripts/ui-idle-memory.py --before dist/BASELINE_STAGE --after dist/STAGE --output docs/UI_MEMORY.json
```

Use the staging directory printed by the build. Scripts restore stage settings.
Artifacts: `dist/VYNX-ARC-Portable-x64.zip`, `dist/VYNX-ARC-Setup-x64.exe`,
`dist/VYNX-ARC-Explorer-x64.msix` and `dist/SHA256SUMS.txt`. Packages are unsigned;
MSIX is unregistered. No certificate/trust settings were installed.
