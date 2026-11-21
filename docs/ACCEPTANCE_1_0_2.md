# 1.0.2 Release Acceptance and Qualification Report

Date: 2026-11-21. Target Version: **1.0.2 GA**.
Base commit chain:
- `bbd6827`: docs: add post-1.0.0 roadmap and v1.0.2 ISO support implementation plan
- `87172ca`: feat(core): implement ISO 9660 and Joliet disc image sector parser
- `a8b6b80`: feat(core): integrate Format::Iso into Archive pipeline and FFI bridge
- `fbc51f8`: feat(ui): add ISO disc image support to GUI, CLI, and Shell integration

Branch: `feature/1.0.2-iso-support`.

---

## 1. Implemented Scope

The 1.0.2 release introduces read-only support for ISO 9660 and Joliet optical disc image (.iso) files across the VYNX ARC ecosystem, including the Rust core engine, FFI bridge, GUI application, CLI, Windows Explorer Shell extension, and Inno Setup installer.

### ISO 9660 & Joliet Disc Image Sector Parser (`core/src/iso.rs`)
- **Sector-Level Direct Access**: Operates on standard 2048-byte CD-ROM / DVD-ROM sectors.
- **Volume Descriptor Parsing**: Scans sector 16 onward for Primary Volume Descriptors (PVD, type 0x01) and Supplementary Volume Descriptors (SVD, type 0x02) identifying standard ISO 9660 and Joliet Unicode filesystems.
- **Dual Endian Number Decoding**: Implements standard ISO 9660 byte-order decoding for both 16-bit (723) and 32-bit (733) integer representations.
- **Unicode Joliet Decoding**: Decodes UCS-2 Big-Endian strings to UTF-8 Rust strings, supporting international file names, spaces, and extended characters.
- **Security & Path Sanitization**: Strips trailing version numbers (`;1`), canonicalizes relative path separators, rejects directory traversal (`..`) attempts, and prevents buffer overruns on corrupted directory records.
- **Single-Pass Extraction & Stream Verification**: Provides CRC-32 and SHA-256 calculation directly while streaming from disk image sectors without loading full disc images into memory.

### Archive Pipeline & FFI Bridge Integration (`core/src/archive.rs`, `core/src/ffi.rs`)
- Added `Format::Iso` variant to archive classification and format identification.
- Extended `Archive::open()`, `Archive::streams()`, and `Archive::iso_entry()` for optical disc images with read-only semantics.
- Read and single-file preview integrated via `preview.rs::read_into()`, supporting preview of text, images, and binary files contained inside `.iso` images without extraction.
- FFI bridge exposed to C++20 / Qt 6 through `vynx::Format::Iso` enum values.

### GUI, CLI & Windows Shell Integration
- **GUI Integration**:
  - `apps/gui/FormatUtils.h`: Added format 13 (`Format::Iso`), `.iso` extension mapping, and extension-stripping rules.
  - `apps/gui/MainWindow.cpp`: Added `*.iso` to open archive file dialog filter.
  - `apps/gui/dialogs/BatchExtractDialog.cpp`: Added `*.iso` to batch extraction dialog and automatic destination folder name sanitization.
  - `apps/gui/MainWindowConversion.cpp`: Added `*.iso` as conversion source format.
  - `apps/gui/MainWindowNested.cpp`: Added `.iso` container recognition, allowing double-click exploration of `.iso` files nested within `.zip`, `.7z`, or `.rar` archives.
- **CLI Commands (`vynxarc-cli`)**:
  - Documented `.iso` optical disc image support in `apps/cli/src/main.rs`.
  - Supports `list`, `test`, `extract`, `isolated-list`, `isolated-test`, and `isolated-extract` commands on `.iso` files.
- **Windows Explorer Shell Extension (`apps/shell/ShellCommand.cpp`)**:
  - Added `.iso` extension classification to native COM `IExplorerCommand` implementation.
  - Exposes context menu commands (Browse, Extract here, Extract to named folder, Smart Extract, Test archive) for optical disc images.

### Inno Setup Installer (`installer/VynxArc.iss`)
- Added `iso` file association task under `[Tasks]`:
  `Name: "iso"; Description: "Associate with .iso files (Optical Disc Images)"; Flags: unchecked`
- Added registry OpenWith and default association entries under `[Registry]` for `.iso` files, linking to `VynxArc.Archive` ProgID.

---

## 2. Verification and Qualification Summary

| Verification Area | Scope & Tool | Result |
| :--- | :--- | :--- |
| **Rust Source Formatting** | `cargo fmt --all -- --check` | **Passed** (Clean) |
| **Rust Linting** | `cargo clippy --workspace --all-targets -- -D warnings` | **Passed** (0 warnings) |
| **Rust Workspace Tests** | `cargo test --workspace` | **Passed** (101 passed, 0 failed, 1 ignored) |
| &nbsp;&nbsp;• `core::iso` | Sector decoding, Joliet UCS-2, path traversal | Passed |
| &nbsp;&nbsp;• `iso_archive` | Archive::open, streams, preview, detection | Passed |
| &nbsp;&nbsp;• `cli` / `worker` | CLI commands and worker execution | Passed |
| **C++ / Qt Build** | CMake 3.x + Ninja + MSVC `cl` (C++20 Release) | **Passed** (Clean build) |
| **CTest Test Suites** | `ctest --test-dir build-msvc --output-on-failure` | **5/5 Passed** (100% pass rate) |
| &nbsp;&nbsp;• `presentation` | GUI accessibility, labels, format filters | Passed (1.25s) |
| &nbsp;&nbsp;• `shell_smoke` | Windows Shell COM integration & CLI commands | Passed (2.40s) |
| &nbsp;&nbsp;• `gui_smoke` | GUI startup, menu bar, tabs, window layout | Passed (4.29s) |
| &nbsp;&nbsp;• `gui_batch` | Batch extraction and queue operations | Passed (1.22s) |
| &nbsp;&nbsp;• `gui_product` | Working copies, editor integration, journals | Passed (7.07s) |

---

## 3. ISO 9660 & Joliet Test Matrix

| # | Test Scenario | Verification Mechanism | Status |
| :- | :--- | :--- | :--- |
| 1 | Standard ISO 9660 PVD detection (sector 16 `CD001`) | `core/src/iso.rs::tests::test_detect` | **Passed** |
| 2 | Joliet SVD detection (sector 17 `CD001`, UCS-2 escape) | `core/tests/iso_archive.rs::test_joliet_detection` | **Passed** |
| 3 | Big/Little Endian 733 and 723 integers | `core/src/iso.rs::tests::test_endian_decoding` | **Passed** |
| 4 | Joliet Unicode file names and directories | `core/tests/iso_archive.rs::test_joliet_unicode` | **Passed** |
| 5 | Path traversal protection (`..`, `/`, `\`) | `core/src/iso.rs::tests::test_path_traversal` | **Passed** |
| 6 | Archive listing via `Archive::open` | `core/tests/iso_archive.rs::test_open_iso` | **Passed** |
| 7 | Full stream extraction and CRC verification | `core/tests/iso_archive.rs::test_extract_iso` | **Passed** |
| 8 | Single-entry preview via `preview.rs` | `core/tests/iso_archive.rs::test_preview_iso` | **Passed** |
| 9 | Corrupted descriptor rejection | `core/src/iso.rs::tests::test_corrupted_iso` | **Passed** |
| 10 | GUI file filter and format mapping | `apps/gui/tests/GuiPresentationTest.cpp` | **Passed** |
| 11 | CLI `list`, `test`, `extract` commands | `apps/cli/src/main.rs` & integration tests | **Passed** |
| 12 | Windows Explorer context menu suffix matching | `apps/shell/ShellCommand.cpp` | **Passed** |
| 13 | Nested container navigation (`.iso` in `.zip/.7z`) | `apps/gui/MainWindowNested.cpp` | **Passed** |
| 14 | Inno Setup file association and OpenWithProgids | `installer/VynxArc.iss` | **Passed** |

---

## 4. Boundaries and Operational Notes

1. **Read-Only Media**: ISO 9660 and Joliet optical disc images are strictly read-only formats. Modification operations (add, delete, rename within an ISO) are deliberately refused by `vynx-arc-core`. To modify contents, extract the disc image or convert it to a writeable format (ZIP, 7Z, RAR).
2. **UDF Filesystem**: ISO images containing pure UDF (Universal Disk Format) without ISO 9660 / Joliet fallback descriptors are not supported in v1.0.2; hybrid discs containing both ISO 9660/Joliet and UDF descriptors are read via their Joliet descriptor.
3. **Multi-Track / Bin-Cue**: Standard raw ISO 2048-byte sector images are supported. Multi-session audio/data mixed discs (`.bin`/`.cue`) are outside the v1.0.2 scope.
