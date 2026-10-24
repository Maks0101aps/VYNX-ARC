# Current status: 0.9.0 development candidate

Native RAR5 implementation and boundaries: [NATIVE_RAR.md](NATIVE_RAR.md).
Exact verification and open release gates: [ACCEPTANCE_0_9.md](ACCEPTANCE_0_9.md).
No stable release, push, tag or publication is claimed.

**RAR support in 0.9.0 is implemented and independently qualified:** RAR4/RAR5
reading; native RAR5 compressed creation with solid, AES/hidden filenames,
dictionary controls, recovery records and real volumes; verified single-volume
RAR5 rebuild modifications. WinRAR21/21 and a real20,000-file round-trip passed.
RAR4 writing, multipart modification and recovery-repair UI are not supported.

Follow-up: scoped real Narrator/keyboard workflow passed; unsigned and separate
TESTONLY-signed packages passed host checks. Test signatures are not publicly
trusted. Passed on [`ed65934`](https://github.com/Maks0101aps/VYNX-ARC/commit/ed65934a100a4783562d17e6e469d24dda226fbd): [run40082225124](https://github.com/Maks0101aps/VYNX-ARC/actions/runs/40082225124), source110, CTest5/5, nativeRAR21 and1,000-file round-trip. Details and exact limits are in the acceptance report.
No Sandbox result is claimed; the host/remote-runner evidence has its own scope.

The following 0.8 report is historical baseline evidence.


# Implementation status — 2026-10-23

Version: **0.8.0 release candidate**. The accepted baseline is 0.7.0.
Automated source/package gates passed on the development host; exact package
identities and open gates are recorded in
[MILESTONE_0_8.md](MILESTONE_0_8.md), with raw measurements in
[BENCHMARKS_0_8.md](BENCHMARKS_0_8.md). The [0.8.0 plan](ROADMAP_0_8.md) and
[changelog](CHANGELOG.md) track the increment from 0.7.0. Earlier reports retain
historical counts and failures. This is not acceptance of the entire V1 brief.

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
