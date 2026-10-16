# Implementation status - 2026-10-16

Version remains **0.1.0 development**. Public stable-release acceptance is not
complete. This file describes the current main checkout; historical measurements
and failures remain in their dated reports.

## Implemented

- Shared Rust archive engine, CXX bridge, native Qt Widgets GUI and Rust CLI.
- Browse/test/extract ZIP, 7Z, TAR, TAR.GZ and native RAR4/RAR5. Create ZIP/7Z/TAR/
  TAR.GZ, including ZIP/7Z encryption. RAR is read-only.
- Transactional ZIP/7Z add/delete/rename, drag-to-add, independent replacement
  verification, cancellation cleanup and free-space preflight.
- Split-7Z read/create and RAR multipart read with missing-volume errors.
- Smart Extract; Ask/Replace/Skip/Keep both/Newer/Stop conflicts and apply-all.
- SHA-256/CRC32 for archive files and decoded entries, supplied-hash verification.
- Whole-archive file search by name/path, wildcard matching, path column and
  restoration of the current folder when search is cleared.
- Read-only UTF-8 text preview up to 1 MiB, via double-click, Enter or F3 where
  applicable. No plaintext file or external application is used for preview.
- Named selection actions; worker/progress/conflict orchestration lives in
  MainWindowJobs.cpp and text preview in MainWindowPreview.cpp.
- English/Ukrainian/Russian UI, theme settings, local history, keyboard navigation.
- Source quality/audit/test gates and portable/installer/MSIX build scripts.
- Native Explorer COM/IPC component and optional per-user Open With registration;
  real signed modern Explorer activation is still unverified.

## Changes and verification in this pass

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
- Review and selectively integrate feature/0.3-development: wider compression
  formats, compression presets, resource modes and the operation queue already
  exist there; they are not part of this main-checkout change.

## Known behavior and compatibility limits

Extraction commits per file; completed files remain after cancellation. Archives
containing links are rejected for write/extract/test operations. Solid archives
may decode unrelated streams, including during preview. Preview accepts UTF-8
text only and never executes content. Backend error details can remain English.
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
