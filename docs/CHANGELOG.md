# Changelog

## 0.6.0 candidate - 2026-10-19

- Move GUI archive reads and writes into bounded, supervised worker processes;
  retain other tabs and working copies after worker failure.
- Reduce selected extraction from a 100,000-entry ZIP from 40.14 to 1.78 seconds
  in the core benchmark; actual GUI median is 3.32 seconds.
- Persist complete large selections, acknowledge publication only after durable
  intent recording, and verify unknown write results before explicit retry.
- Add planned single/batch conversion into verified ZIP/7Z with separate source
  and destination credentials, explicit decryption consent and failed-only retries.
- Add a working-copy dock, content verification, storage admission budget and
  Save as new archive; reconcile interrupted edit saves without losing newer edits.
- Add queue filters, accurate state counters, scrolling docks and RU/UK translations.
- Fix installer Open With values; validate the real 0.5-to-0.6 upgrade and uninstall.
- Exercise actual VS Code/Writer saves, independent archive readers, GUI fault
  injection, large-archive/retention benchmarks and extended mutation campaigns.

See [development evidence](DEVELOPMENT_0_6.md) and [benchmarks](BENCHMARKS_0_6.md).
Final package acceptance is in progress.

## 0.5.0 development - 2026-10-17

- Add archive tabs, isolated session state, durable reviewable queue history and
  explicit ZIP/7Z working-copy saveback with content/source guards.
- Add archive-wide search, selected ZIP preview and retained edit recovery.
- Validate packages and the earlier 0.4-to-0.5 installer upgrade.

See the historical [0.5 report](MILESTONE_0_5.md).

## 0.4.0 development - 2026-10-16

- Add verified bounded file content and external materialization to the Rust/CXX API.
- Replace text-only GUI preview with searchable UTF-8/UTF-16 text, raster image
  zoom/fit and bounded Hex tabs; keep HTML/SVG passive and never execute content.
- Add explicit OS-associated opening of supported document/media types through
  read-only, ZoneId=3 temporary copies; manage owned cache lifetimes with locks.
- Add batch extraction with per-archive destinations, shared optional password,
  explicit conflict policies and continued dispatch after individual failures.
- Add queue pause/resume, reordering, cancel-all and readable error details;
  capture creation settings before queuing and release secrets on cancellation.
- Exercise real batch dispatch, paused queues, corrupt/encrypted materialization,
  viewer limits, Unicode decoding, image limits and OS URL dispatch interception.
- Update GUI, CLI, Cargo packages, installer and Explorer identity to 0.4.0.

See [MILESTONE_0_4.md](MILESTONE_0_4.md) for current evidence and boundaries.

## 2026-10-16 - product development increment, still 0.1.0

- Integrate stream formats GZIP/XZ/BZIP2/ZSTD/LZMA and TAR.XZ/TAR.BZ2/TAR.ZST
  while retaining the current ZIP 9.0.4 and patched 7Z parser protections.
- Add real compression presets and codec-aware resource profiles to creation,
  plus default resource preferences and CLI controls.
- Add a bounded operation queue dock, multi-archive opening, pending-job
  cancellation and completed-history cleanup.
- Show operation phases, effective configuration and cancellation state; reset
  rate/ETA when a phase changes.
- Validate new streams with independent Python encoders/decoders, encrypted
  Store archives, all-format text preview and real asynchronous GUI queue tests.

See [PRODUCT_DEVELOPMENT.md](PRODUCT_DEVELOPMENT.md) for usage and evidence.

## 2026-10-16 - local stabilization candidate, still 0.1.0

- Upgrade ZIP to stable 9.0.4 and fail source audit on yanked dependencies.
- Bound ZIP/ZIP64 metadata before parsing; reject ambiguous end-record fallback.
- Add documented local 7Z header/metadata/dictionary-chain budget enforcement.
- Search file names/paths across the archive with full result paths.
- Add verified read-only UTF-8 text preview without temporary plaintext files.
- Classify modification errors and offer explicit archive reload, without retries.
- Name selection actions and separate job orchestration and preview source files.
- Add cross-process lock checks, bounded parser mutation smoke and packaged CI QA.
- Prepare clean-Windows installer acceptance; current host Sandbox launch failed.

See [the dated report](IMPROVEMENTS_2026-10-16.md) for evidence and release limits.

## Unreleased вЂ” work toward 0.2.0

- Separate compact Home and archive workspace into native Widgets pages; move
  creation, extraction, conflict, settings and about presentation into dialogs.
- Add vector navigation/file icons, clickable collapsing breadcrumbs, responsive
  search, shared selection-aware command controls and narrow-window overflow.
- Sort packed sizes, ratios and timestamps numerically; show unknown/default DOS
  dates as a dash. Add cached totals, quiet notices and smoothed cautious ETA.
- Polish light/dark palettes, recent archive rows, locale-aware column sizing and
  format-authoritative creation. Add presentation tests and deployed visual/RAM
  runners; retain the archive core and mandatory extraction protection.

- Show verification status, algorithm, actual hash and expected hash in GUI results
  and mismatch diagnostics without hashing the archive a second time.

- Read numbered split-7Z archives and create verified 7Z volumes with GUI/CLI size
  selection, bounded part indexing, no-clobber publication and cancellation cleanup.
- Report exact missing parts for split-7Z and native RAR4/RAR5 multivolume decoding.
- Keep all RAR fixture binaries local; restore pinned, SHA-256 checked data for CI.
- Serialize bundled UnRAR decoder lifetimes to protect its process-global error handler.
- Expand security policy regressions for entry-count, total-size, ratio and duplicate-path limits.
- Measure three trials per packaged archive dataset and record median throughput, CPU and RSS.

- Migrate Qt, CXX and Rust to the MSVC ABI and deploy app-local VC143 runtime.
- Streaming transactional ZIP/7Z add, delete and rename with encrypted archives,
  verified replacement and cancellation cleanup. Publication reports the first
  failure without lock retries or sleeps.
- Add GUI modification commands and drag-to-add, with read-only explanations.
- Make the selected GUI creation format authoritative and normalize extensions.
- Preflight free space for extraction, creation and archive replacement.
- Detect changed entry counts and ZIP CRC metadata during stream operations.
- Add asynchronous extraction conflict comparison, apply-all, Keep both and
  newer-time decisions; restore regular-file modified timestamps.
- Hash selected decoded files without plaintext staging and verify supplied
  SHA-256 / CRC32 digests for archive files or entries through GUI and CLI.
- Add a codec-free native IExplorerCommand component, bounded private selection
  IPC, sparse identity packaging and guarded registration/unregistration scripts.
  Signed registration and real Explorer/clean-VM acceptance remain unverified.
- Share a non-copyable wiping password buffer across C++ workers/reopen callbacks
  and document Qt/CXX/Rust/backend password lifetimes without claiming full erasure.
- Bound Mark-of-the-Web reads before allocating, including a growing-stream check.
- Resolve existing publication parents to extended Windows paths for physical
  paths longer than 260 characters, preserving ancestor/link checks and no-clobber.
- Add opt-in publication lifecycle/kernel-handle/Restart Manager diagnostics,
  sequence and stress runners, and immutable per-run verification evidence.
- Align CI, local checks and release packaging with fmt, strict Clippy, locked
  Rust tests, dependency audit, native build and CTest. Correct the format matrix,
  RAR concurrency diagnosis and MSVC dependency/runtime notices.

## 0.1.0 - 2026-10-02 (development)

- Native C++20 Qt 6 Widgets application and typed Rust/CXX core.
- Real ZIP, 7Z, RAR4/RAR5, TAR and TAR.GZ reading, testing and extraction.
- ZIP AES and 7Z encryption creation; ZIP/7Z/TAR/TAR.GZ creation.
- Windows-aware path protection, conservative link blocking, output limits,
  cancellation and publication after checksum/size validation.
- Folder browsing, filtering, multi-selection, local recent history, themes,
  Ukrainian and Russian application translations.
- CLI, developer scripts, CI configuration and portable/installer release pipeline.

Not a completed V1; see docs/IMPLEMENTATION_STATUS.md for remaining work.
