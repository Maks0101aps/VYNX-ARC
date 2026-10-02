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

## Functional regression

- Final release build: **41 Rust tests** and **3 CTest targets passed**:
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
- The deployed verifier restricts PATH to Windows/System32 and removes developer
  Qt variables. It independently reads ZIP/7Z, checks ZIP/7Z/TAR/TAR.GZ round trips,
  real RAR fixtures, add/delete/rename/hash/verify and split-7Z/missing parts.
  Smart Extract now checks actual bytes and absence of duplicate root nesting.
  `VERIFICATION.json` records the **failed final run** and completed checks before
  its failure; `VERIFICATION_LAST_SUCCESS.json` retains the earlier complete run
  on the same archive core/presentation. The final run passed GUI/CLI smoke and
  four format round trips/Smart Extract before failing during ZIP replacement.
  Full final packaged modification acceptance is therefore blocked.

## Measurements and limitations

`UI_MEMORY.json` records three fresh launches for each revision/state/theme, the
same small ZIP, English, 100% scaling and isolated PATH. Working set is sampled
after five seconds settling and one second CPU observation. A 100ms startup poll
is an observed peak, not an exact maximum. `PERFORMANCE.md` summarizes medians.
These measurements do not establish throughput or large-archive memory bounds.

The intermittent ZIP replacement `AccessDenied` recurred during additional
packaged runs, including the final run after successful candidate verification;
the operation reported that the original was preserved. The cause remains
undiagnosed. Earlier native RAR open code 15 is also unresolved. No codec/security
changes were made to force a pass. Passing source tests do not supersede the
failed final deployed modification check.

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
