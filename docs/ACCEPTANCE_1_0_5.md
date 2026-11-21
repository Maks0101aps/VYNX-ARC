# 1.0.5 Release Acceptance and Qualification Report

Date: 2026-11-21. Target Version: **1.0.5 GA**.
Base commit chain:
- `6111758`: docs: add v1.0.5 WIM and ESD support implementation plan
- `d8d1122`: feat(core): implement WIM & ESD header and XML metadata parser
- `0370f60`: feat(core): integrate Format::Wim and native wimgapi extraction pipeline
- `0d4f53e`: feat(ui): add WIM & ESD image support to GUI, CLI, and Shell integration

Branch: `feature/1.0.5-wim-support`.

---

## 1. Implemented Scope

The 1.0.5 release introduces comprehensive read-only support for Windows Imaging Format (`.wim`), Electronic Software Download (`.esd`), and Split WIM (`.swm`) archives across the VYNX ARC ecosystem, including the Rust core engine, FFI bridge, GUI application, CLI, Windows Explorer Shell extension, and Inno Setup installer.

### WIM & ESD Binary Header & XML Metadata Parser (`core/src/wim.rs`)
- **Header Structure Parsing**: Parses standard 208-byte WIM header magic (`MSWIM\0\0\0`), version (`0x00010d00`), compression flags (XPRESS, LZX, LZMS), part numbering (`part_number` / `total_parts`), image count, and resource descriptors.
- **XML Metadata Extraction**: Decodes UTF-16LE XML metadata directly from WIM resource descriptors, extracting `<IMAGE>` entries, index numbers, image names, descriptions, total uncompressed bytes, file count, and directory count.
- **Fail-Closed Security & Path Sanitization**: Image entries and directory paths are strictly validated through `crate::security::validate_name()`. Path traversal attempts (`..`), backslashes, absolute paths, and excessive depth (> 64 levels) are rejected with `CORRUPT` error codes.
- **Multi-Image Virtual Root Organization**: Multi-image WIMs (e.g., Windows 10/11 multi-edition installation media) are cleanly organized under virtual image root directories (`[1] Image Name/`, `[2] Image Name/`) to allow intuitive browsing and collision-free extraction.

### Native Windows WIMGAPI Bindings & Extraction Pipeline (`core/src/wim_native.rs`, `core/src/archive.rs`, `core/src/operations.rs`, `core/src/ffi.rs`)
- **Dynamic `wimgapi.dll` Integration**: Safely and dynamically loads Windows Imaging API functions (`WIMCreateFile`, `WIMCloseHandle`, `WIMLoadImage`, `WIMExtractImage` / `WIMApplyImage`, `WIMGetImageCount`) from `System32` at runtime. No static link-time dependency or Windows SDK headers required.
- **Fail-Closed Error Handling**: Translates Win32 error codes (`GetLastError()`) into descriptive `ArcError` types. If `wimgapi.dll` is absent or entry points cannot be loaded, clear diagnostics are reported without crashing.
- **Read-Only Media Protection**: `Format::Wim` is strictly marked immutable (`archive.writable == false`). Mutation attempts (add, delete, rename within WIM/ESD) are immediately rejected by `vynx-arc-core`.
- **Preview & Stream Access**: Integrated with `preview.rs` to allow safe, fast single-file previewing of text, image, and binary files contained within WIM archives without extracting the whole image.
- **FFI Bridge Integration**: Exposes `Format::Wim` to C++20 / Qt 6 with format name `"WIM"`.

### GUI, CLI & Windows Shell Integration
- **GUI Integration**:
  - `apps/gui/FormatUtils.h`: Registered format 14 (`Format::Wim`), extension mapping (`.wim`, `.esd`, `.swm`), and extension-stripping rules.
  - `apps/gui/MainWindow.cpp`: Added `*.wim *.esd *.swm` to open archive file dialog filter.
  - `apps/gui/dialogs/BatchExtractDialog.cpp`: Added `*.wim *.esd *.swm` to batch extraction dialog with automated destination folder naming.
  - `apps/gui/MainWindowConversion.cpp`: Added `.wim`, `.esd`, `.swm` as source formats for conversion to writeable archives (ZIP, 7Z, RAR).
  - `apps/gui/MainWindowNested.cpp`: Added `.wim`, `.esd`, `.swm` container recognition, allowing seamless exploration of WIM/ESD images nested inside other archives (`.zip`, `.7z`, `.rar`, `.iso`).
- **CLI Commands (`vynxarc-cli`)**:
  - Documented `.wim`, `.esd`, and `.swm` support in `apps/cli/src/main.rs`.
  - Supports `list`, `test`, `extract`, `isolated-list`, `isolated-test`, and `isolated-extract` commands for WIM/ESD/SWM files.
- **Windows Explorer Shell Extension (`apps/shell/ShellCommand.cpp`)**:
  - Added `.wim`, `.esd`, and `.swm` extension classification to native COM `IExplorerCommand` implementation.
  - Exposes context menu commands (Browse, Extract here, Extract to named folder, Smart Extract, Test archive) for Windows Imaging files.

### Inno Setup Installer (`installer/VynxArc.iss`)
- Bumped `AppVersion` to `1.0.5`.
- Updated setup welcome messages in English, Ukrainian, and Russian to announce v1.0.5 and WIM/ESD/SWM format capabilities.
- Added file association tasks under `[Tasks]`:
  - `Name: "wim"; Description: "Associate with .wim files (Windows Imaging Format)"; Flags: unchecked`
  - `Name: "esd"; Description: "Associate with .esd files (Electronic Software Download)"; Flags: unchecked`
- Added OpenWith registry entries for `.wim`, `.esd`, and `.swm` under `[Registry]`.
- Added direct file association registry keys for `.wim` and `.esd` linked to `VynxArc.Archive` ProgID.

---

## 2. Verification and Qualification Summary

| Verification Area | Scope & Tool | Result |
| :--- | :--- | :--- |
| **Rust Source Formatting** | `cargo fmt --all -- --check` | **Passed** (Clean) |
| **Rust Linting** | `cargo clippy --workspace --all-targets -- -D warnings` | **Passed** (0 warnings) |
| **Rust Workspace Tests** | `cargo test --workspace` | **Passed** (112 passed, 0 failed, 1 ignored) |
| &nbsp;&nbsp;• `core::wim` | Header parser, XML metadata, endianness, path security | Passed |
| &nbsp;&nbsp;• `wim_archive` | Archive::open, read-only constraints, native API loading | Passed |
| &nbsp;&nbsp;• `core::iso` / `iso_archive` | Sector decoding, Joliet Unicode, ISO archive pipeline | Passed |
| &nbsp;&nbsp;• `cli` / `worker` | CLI commands and isolated worker execution | Passed |
| **C++ / Qt Build** | CMake 3.x + Ninja + MSVC `cl` (C++20 Release) | **Passed** (Clean build) |
| **CTest Test Suites** | `ctest --test-dir build-msvc --output-on-failure` | **5/5 Passed** (100% pass rate) |
| &nbsp;&nbsp;• `presentation` | GUI accessibility, labels, format filters (`*.wim`, `*.esd`, `*.swm`) | Passed (1.41s) |
| &nbsp;&nbsp;• `shell_smoke` | Windows Shell COM integration & CLI commands | Passed (2.50s) |
| &nbsp;&nbsp;• `gui_smoke` | GUI startup, menu bar, tabs, window layout | Passed (4.52s) |
| &nbsp;&nbsp;• `gui_batch` | Batch extraction and queue operations | Passed (1.42s) |
| &nbsp;&nbsp;• `gui_product` | Working copies, editor integration, journals | Passed (7.23s) |

---

## 3. WIM & ESD Test Matrix

| # | Test Scenario | Verification Mechanism | Status |
| :- | :--- | :--- | :--- |
| 1 | Standard WIM magic detection (`MSWIM\0\0\0`) | `core/tests/wim.rs::test_wim_detect_and_header` | **Passed** |
| 2 | Corrupted WIM magic rejection | `core/tests/wim.rs::test_wim_corrupt_magic` | **Passed** |
| 3 | Truncated header rejection (< 208 bytes) | `core/tests/wim.rs::test_wim_truncated_header` | **Passed** |
| 4 | Out-of-bounds XML resource descriptor offset handling | `core/tests/wim.rs::test_wim_xml_out_of_bounds` | **Passed** |
| 5 | UTF-16LE XML metadata extraction and `<IMAGE>` tags | `core/tests/wim.rs::test_wim_detect_and_header` | **Passed** |
| 6 | Multi-image WIM index parsing and virtual roots | `core/tests/wim.rs::test_wim_multi_image_list` | **Passed** |
| 7 | Path traversal protection (`..`, `/`, `\`) in image names | `core/tests/wim.rs::test_wim_unsafe_image_name_rejected` | **Passed** |
| 8 | Standard disk resource descriptor offset decoding | `core/tests/wim.rs::test_wim_standard_disk_reshdr` | **Passed** |
| 9 | Archive pipeline `Archive::open()` format detection | `core/tests/wim_archive.rs::test_wim_archive_pipeline` | **Passed** |
| 10 | Strict read-only constraint enforcement | `core/tests/wim_archive.rs::test_wim_archive_read_only_constraints` | **Passed** |
| 11 | Dynamic `wimgapi.dll` loading on Windows 10/11 | `core/tests/wim_archive.rs::test_wim_native_api_loading` | **Passed** |
| 12 | Native extraction error handling with invalid handles | `core/tests/wim_archive.rs::test_wim_extract_image_error_handling` | **Passed** |
| 13 | FFI bridge properties and format string `"WIM"` | `core/src/ffi.rs::tests::test_ffi_wim_properties` | **Passed** |
| 14 | GUI format filter and `FormatUtils` mapping (`wim`, `esd`, `swm`) | `apps/gui/tests/GuiPresentationTest.cpp` | **Passed** |
| 15 | Shell context menu extension matching (`.wim`, `.esd`, `.swm`) | `apps/shell/ShellCommand.cpp` & `VynxShellSmoke` | **Passed** |
| 16 | Nested container navigation (`.wim` / `.esd` inside archives) | `apps/gui/MainWindowNested.cpp` | **Passed** |
| 17 | Inno Setup file associations and OpenWith registry keys | `installer/VynxArc.iss` | **Passed** |

---

## 4. Boundaries and Operational Notes

1. **Read-Only Media**: Windows Imaging Format (`.wim`, `.esd`, `.swm`) archives are strictly read-only within VYNX ARC. Destructive modifications (adding, deleting, or renaming files within an existing WIM) are deliberately rejected to prevent corruption of internal WIM lookup tables and duplicate file deduplication streams. To modify contents, extract the image or convert it to a writeable format (ZIP, 7Z, RAR).
2. **Native `wimgapi.dll` Dependency**: Image extraction leverages Microsoft's native Windows Imaging API (`wimgapi.dll`), present by default in `%SystemRoot%\System32` on Windows 10, Windows 11, and Windows Server 2016+. On non-Windows platforms or in minimal environments where `wimgapi.dll` is unavailable, WIM archives can still be opened, browsed, and inspected via the pure Rust parser, while extraction operations fail gracefully with clear diagnostics.
3. **Split WIM (`.swm`) References**: Individual Split WIM parts can be inspected; applying an image spanning multiple split parts requires all sibling parts (`.swm`, `.swm2`, etc.) to be present in the same directory.
