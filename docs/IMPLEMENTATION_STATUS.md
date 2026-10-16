# Implementation status - 2026-10-16

Version is **0.4.0 development**. Public stable-release acceptance is not
complete. This file describes the current main checkout; historical measurements
and failures remain in their dated reports.

## Implemented

- Shared Rust archive engine, CXX bridge, native Qt Widgets GUI and Rust CLI.
- Browse/test/extract ZIP, 7Z, TAR, TAR.GZ, TAR.XZ, TAR.BZ2, TAR.ZST,
  GZIP/XZ/BZIP2/ZSTD/LZMA and native RAR4/RAR5. Create all except RAR,
  including ZIP/7Z encryption. RAR is read-only.
- Compression presets, codec-aware effective settings, three resource modes,
  phase progress and a bounded queue with pause/resume, reordering, pending
  cancellation, error details and creation-job dispatch.
- Transactional ZIP/7Z add/delete/rename, drag-to-add, independent replacement
  verification, cancellation cleanup and free-space preflight.
- Split-7Z read/create and RAR multipart read with missing-volume errors.
- Smart Extract; Ask/Replace/Skip/Keep both/Newer/Stop conflicts and apply-all.
- SHA-256/CRC32 for archive files and decoded entries, supplied-hash verification.
- Whole-archive file search by name/path, wildcard matching, path column and
  restoration of the current folder when search is cleared.
- Read-only text/image/Hex preview via double-click, Enter or F3. UTF-8/UTF-16
  text search, image zoom and bounded rendering; no plaintext preview file.
- Explicit external opening through verified temporary copies for supported
  document/media types, with read-only attributes and Windows ZoneId=3.
- Batch extraction into per-archive folders; failures are recorded and remaining
  jobs continue. A shared optional password stays in memory only.
- Named selection actions; worker/progress/conflict orchestration lives in
  MainWindowJobs.cpp and text preview in MainWindowPreview.cpp.
- English/Ukrainian/Russian UI, theme settings, local history, keyboard navigation.
- Source quality/audit/test gates and portable/installer/MSIX build scripts.
- Native Explorer COM/IPC component and optional per-user Open With registration;
  real signed modern Explorer activation is still unverified.

See [MILESTONE_0_4.md](MILESTONE_0_4.md) for the latest product
feature increment and its validation.

## Previous hardening pass

ZIP is pinned to stable 9.0.4, replacing yanked 8.6.0. The audit gate now denies
any yanked dependency. ZIP directory preflight and a documented local 7Z parser
patch enforce header/metadata and dictionary-chain budgets before affected
allocations. These are not whole-process memory limits.

See [IMPROVEMENTS_2026-10-16.md](IMPROVEMENTS_2026-10-16.md) for the final validation
results, exact artifacts and remaining environment gates. The GitHub workflow
now packages/verifies the deployed app and runs external-lock/mutation checks;
that workflow has not been executed remotely in this session.

## Remaining release work

- Clean Windows acceptance: portable, per-user installer, Open With, previous-
  version upgrade, uninstall/reboot cleanup and actual Explorer menu behavior.
  scripts/check-clean-windows.ps1 prepares and can launch a Windows Sandbox run;
  its guest script covers an automated subset only. The guest report and user
  token must be reviewed; Sandbox is not a substitute for standard-user VM QA.
- Trusted signing/identity and signed modern Explorer activation.
- Resolve the historical ZIP replacement failure causally. The original package
  passed today's project-path verification and 1,400-publication stress run;
  this does not explain the failures captured on October 3.
- Broader parser/codec/native allocation and CPU budgets, TAR/PAX review,
  coverage-guided fuzzing, Unicode identity cases, hostile filesystem races and
  backend/Qt password-copy zeroization.
- Metadata preservation review, real large/long-running archive performance,
  repeated open/close memory retention, keyboard/accessibility/native-frame QA.
- Profile large archives; refine durable queue history, codec resource estimates
  and modification compression options.

## Known behavior and compatibility limits

Extraction commits per file; completed files remain after cancellation. Archives
containing links are rejected for write/extract/test operations. Solid archives
may decode unrelated streams, including during preview. Internal preview supports
bounded text/raster/Hex and never executes content; external opening writes an
explicit temporary copy and invokes the OS handler. Backend error details can
remain English.
ZIP metadata preflight deliberately rejects ambiguous end records, trailing data,
non-contiguous/signed central directories and multi-disk ZIP. Parser budgets can
reject otherwise valid very large archives. See SECURITY_MODEL.md for exact
limits and exclusions.

## Historical evidence

- [October 3 acceptance](RELEASE_ACCEPTANCE.md) and
  [root-cause capture](PUBLICATION_ROOT_CAUSE.md): preserved failures, not today's
  current runtime result.
- [UI QA](UI_QA.md) and [performance](PERFORMANCE.md): dated October 2 measurements.
- [Supported formats](SUPPORTED_FORMATS.md), [security model](SECURITY_MODEL.md),
  [password lifetime](PASSWORD_LIFETIME.md), [modification](MODIFICATION.md).
