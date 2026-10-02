# Changelog

## Unreleased — work toward 0.2.0

- Move Qt, CXX and Rust to MSVC; deploy the app-local VC143 runtime.
- Add streaming transactional ZIP/7Z add, delete and rename, including encrypted
  archives, verified replacement, cancellation cleanup and bounded lock retries.
- Add GUI modification commands, drag-to-add and read-only explanations.
- Make selected creation format authoritative and normalize the output extension.
- Preflight disk space for extraction, creation and archive replacement.
- Detect changed archive entry counts and ZIP CRC metadata before stream operations.

## 0.1.0 — development milestone

Initial Qt Widgets / Rust / CXX archive manager with ZIP, 7Z, RAR4/RAR5, TAR and
TAR.GZ browse/test/extract, four writable formats, encrypted ZIP/7Z, cancellation,
path protection, hashing, CLI, themes, translations and Windows packages.
This milestone does not satisfy full V1 acceptance.
