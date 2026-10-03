# Changelog

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
