# Current status: 1.0.0 release

Official 1.0.0 release qualification and package status: [ACCEPTANCE_1_0.md](ACCEPTANCE_1_0.md).
Native RAR5 implementation and boundaries: [NATIVE_RAR.md](NATIVE_RAR.md).
Historical 0.9 qualification: [ACCEPTANCE_0_9.md](ACCEPTANCE_0_9.md).

**Release 1.0.0 is fully implemented and qualified:**
- Complete offline Windows archive manager (Qt 6 Widgets / C++20 / Rust core via CXX).
- Full format support: ZIP, 7Z, RAR4/RAR5, TAR, compressed TAR variants, and standalone compression streams.
- Native RAR5 creation (solid, dictionaries, AES-256 data & header encryption, recovery records, multivolume archives, single-volume verified rebuilds) validated with WinRAR 21/21.
- In-app automatic update system (`UpdateService`, `UpdateDialog`, `UpdateInstaller`) with WinHTTP network queries, SHA-256 package verification, non-intrusive banner notifications, and mode-aware upgrade execution (Inno Setup silent execution for installed mode, atomic batch staging script for portable mode).
- Automated CI/CD release workflow (`.github/workflows/release.yml`) for automated builds, packaging, verification, and GitHub Releases publication on `v*` tags.
- Verified packaging for Windows x64: Inno Setup per-user installer, Portable ZIP with app-local CRT, and MSIX sparse package.

The following reports provide historical baseline evidence.


# Implementation status — 2026-11-21

Version: **1.0.0 general availability**. The accepted baseline is 0.9.0.
Automated source/package gates passed on the development host; exact package
identities and hashes are recorded in [ACCEPTANCE_1_0.md](ACCEPTANCE_1_0.md).

## Available behavior

- Offline Windows Qt/CXX/Rust GUI and CLI; ZIP/7Z/RAR/TAR/stream formats,
  creation/compression/resource modes, hashes, previews and split-7Z remain.
- Independent archive tabs, archive-wide search, navigation/selection, workspace
  restoration and background reads while another tab owns a primary job.
- Supervised GUI archive workers: bounded protocol, two-worker admission, Windows
  memory/process limits, wall-time budget and forced cancellation. Failures retain
  the GUI, other tabs and registered working files.
- Complete large selections, verified sidecars, journal migration, publication
  acknowledgements and read-only verification of unknown archive writes. Explicit
  failed-only retries retain errors and attempt links.
- Planned conversion into new verified ZIP/7Z from supported sources, including
  RAR/TAR. Separate credentials, explicit unencrypted consent, originals retained.
- ZIP/7Z working-copy saveback, Save as new archive, content verification,
  admission budget, last-verification/error details and conflict recovery. Newer
  bytes are never assumed saved during reconciliation.
- EN/UK/RU with complete catalogs for the 0.8.0 dialogs, light/dark/system themes,
  scrollable narrow content, keyboard commands, disposable views and working-copy
  management.
- Portable package, per-user installer and MSIX identity; packaged worker,
  Open With registration, upgrade and uninstall paths are exercised on the
  development host.
- In-app automatic updates with WinHTTP client, SHA-256 verification, and mode-aware
  installation orchestration (silent installer or portable batch updater).

## 1.0.0 changes

- Automated GitHub Actions release pipeline (`.github/workflows/release.yml`)
  building Portable, Setup, and MSIX packages on `v*` tag push.
- In-app `UpdateService` querying GitHub Releases API with native WinHTTP.
- `UpdateDialog` and MainWindow notification banner for update notifications and progress.
- `UpdateInstaller` providing safe handoff for both installed and portable copies.
- Security updates: upgraded `zstd` to 0.14.4 to clear all active advisories.
- Version metadata synchronized to 1.0.0 across all manifests and build configurations.

## 0.8.0 changes

- Localization catalogs include inter-archive copy, extraction recovery and
  temporary storage; `lrelease` reports no unfinished messages.
- Recovery and temporary-storage tables are read-only; key dialogs have accessible
  names/descriptions, label buddies and explicit tab order. Automated presentation
  tests cover these properties.
- `ArchiveJobSpec` centralizes job kinds, states and operation parameters. Journal
  restoration, retry and dispatch use typed comparisons. The `unknown` state is
  valid for recovery, while malformed state strings are rejected.
- Version metadata is consistently 0.8.0 across CMake, Cargo, the installer and
  MSIX manifest.
- `ctutils` is updated from 0.4.2 to 0.4.5 to clear `RUSTSEC-2026-0385`; the
  dependency audit passes with 1350 advisories and 109 crates.

## Evidence and boundaries

0.8.0 passed Rust tests (91 passed, 2 ignored), CTest 5/5, 6/6 publication-lock
checks, 1400/1400 stress publications, 3/3 isolated portable verification and
actual 0.6.0 → 0.8.0 and 0.7.0 → 0.8.0 per-user upgrades. Exact hashes and
boundaries are in the milestone report. The main open performance finding is
roughly doubled sampled RSS for 7Z creation on large/random inputs. The
1000-small-file ZIP extraction result is +4.9% versus 0.6.0 in the recheck; the
earlier +21% was not reproduced and remains only a variance watch item. Narrator,
clean-VM and CI acceptance remain unexecuted.

Worker jobs share the user's filesystem rights and exclude GUI/editor memory.
Preflight/OS caps do not prove a complete allocation or race audit. Qt raster
decoding remains bounded in GUI. Direct CLI commands are not implicitly supervised.

History disabled means session-only queue recovery. Interrupted extraction needs
manual review; physical power loss is unqualified. Working files retain
plaintext/source paths until discarded, independently of history settings.
Fingerprints are hints; saves use hashes. The 2 GiB / 32-copy admission budget
never deletes existing or editor-grown edits.

Packages are unsigned. Trusted Explorer identity installation, SmartScreen,
enterprise policy, independent clean-machine qualification, Narrator review and
physical OS DPI transitions remain separate from host/Qt-scale tests. Full
Rust/native decoder sanitizers and coverage-guided campaigns remain future work.
RAR creation is not implemented.

See [formats](SUPPORTED_FORMATS.md), [security](SECURITY_MODEL.md),
[worker protocol](WORKER_PROTOCOL.md), [0.8 milestone](MILESTONE_0_8.md) and
[0.7 milestone](MILESTONE_0_7.md).
