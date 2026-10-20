# Implementation status — 2026-10-20

Version: **0.6.0 development milestone**. Local release acceptance passed; remote
CI/publication state and exact identities are in [VERIFICATION_0_6.json](VERIFICATION_0_6.json).
The [milestone report](MILESTONE_0_6.md) defines the current boundary. Earlier reports
retain historical counts/failures. This is not acceptance of the entire V1 brief.

## Available behavior

- Offline Windows Qt/CXX/Rust GUI and CLI; existing ZIP/7Z/RAR/TAR/stream formats,
  creation/compression/resource modes, hashes, previews and split-7Z remain.
- Independent archive tabs, archive-wide search, navigation/selection, workspace
  restoration and background reads while another tab owns a primary job.
- Supervised GUI archive workers: bounded protocol, two-worker admission, Windows
  memory/process limits, wall-time budget and forced cancellation. Failures retain
  GUI, other tabs and registered working files.
- Selected extraction of the 100,000-entry ZIP: 1.78 s in core and 3.32 s through
  GUI. Complete name/collision/size and payload checks remain. [Measurements](BENCHMARKS_0_6.md).
- Complete large selections, verified sidecars, journal migration, publication
  acknowledgements and read-only verification of unknown archive writes. Explicit
  failed-only retries retain errors/attempt links. Queue counters and filters.
- Planned conversion into new verified ZIP/7Z from supported sources, including
  RAR/TAR. Separate credentials, explicit unencrypted consent, originals retained.
- ZIP/7Z working-copy saveback, Save as new archive, content verification, admission
  budget, last-verification/error details and conflict recovery. Newer bytes are
  never assumed saved during reconciliation.
- EN/UK/RU, light/dark/system, scrollable narrow content with visible action bars,
  keyboard commands, disposable views and working-copy management.
- Portable/per-user installer, packaged worker, repaired Open With, actual
  0.5-to-0.6 upgrade and uninstall without changing file defaults.

## Evidence and boundaries

Final packages passed isolated SDK-free smoke, independent readers, actual mixed
GUI batch/retry and upgrade checks. Eight worker integration tests, seven GUI
fault/recovery/batch cases, real VS Code/Writer saves, four native CTests, C++ ASan,
mutation and publication/lock/interruption checks are linked from the report.

Worker jobs share the user's filesystem rights and exclude GUI/editor memory.
Preflight/OS caps do not prove a complete allocation or race audit. Qt raster
decoding remains bounded in GUI. Direct CLI commands are not implicitly supervised.

History disabled means session-only queue recovery. Interrupted extraction needs
manual review; no aggregate receipt exists. Physical power loss is unqualified.
GUI adapters still contain closures/JSON despite typed centralized native execution.

Working files retain plaintext/source paths until discarded, independently of
history settings. Fingerprints are hints; saves use hashes. The 2 GiB / 32-copy
admission budget never deletes existing or editor-grown edits. Saveback supports
bounded ZIP/7Z entries without volumes; no auto-merge or hidden save.

Packages are unsigned. Trusted Explorer identity installation, SmartScreen,
enterprise policy, independent clean-machine qualification and physical OS DPI
transitions remain separate from host/Qt-scale tests. Full Rust/native decoder
sanitizers and coverage-guided campaigns remain future work.

See [formats](SUPPORTED_FORMATS.md), [security](SECURITY_MODEL.md),
[worker protocol](WORKER_PROTOCOL.md) and [historical 0.5 report](MILESTONE_0_5.md).
