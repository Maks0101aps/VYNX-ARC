# 1.0.9 Release Acceptance and Qualification Report

Date: 2026-11-22. Target Version: **1.0.9 GA**.
Base commit chain:
- `5f9ff0d`: feat(core): implement transactional multi-volume split staging and two-phase swap engine
- `91e5533`: feat(core): enable safe modification and staged rebuild for multi-volume archives
- `a859a99`: feat(gui,cli): enable UI actions and CLI commands for multi-volume archive modification

Branch: `main`.

---

## 1. Implemented Scope

The 1.0.9 release introduces safe, transactional multi-volume archive modification (Add, Delete, Rename, Replace) with atomic staging and two-phase rollback for `.7z.001`..`.NNN` and `.part1.rar`..`.partN.rar` archives across the VYNX ARC ecosystem, including the Rust core engine, FFI bridge, GUI application, CLI worker, and Inno Setup installer.

### Transactional Split Staging & 3-Phase Swap Engine (`core/src/split_rebuild.rs`)
- **Preflight Disk Space Verification (15% Safety Margin)**:
  - Calculates free disk space on the target volume via `crate::disk::available_space()`.
  - Enforces strict capacity requirements: ensures free space exceeds required rebuild space with a 15% safety buffer (`needed_bytes * 115 / 100`).
  - Rejects operations with `ArcError::DiskExhaustion` before touching any files if storage capacity is insufficient.
- **Transactional 3-Phase Atomic Swap (`atomic_swap_volumes`)**:
  - **Phase 1 (Backup)**: Renames all existing original volumes to unique backup files (`.vynx-bak-<uuid>`).
  - **Phase 2 (Placement)**: Moves all new staged volume parts (`.001.tmp`..`.NNN.tmp`) into final target paths using atomic filesystem replace (`fs::rename` / `MoveFileExW`).
  - **Phase 3 (Verify & Purge)**: Verifies the presence and integrity of all newly placed volume parts and securely removes backup files.
- **Fail-Safe Rollback Engine**:
  - If any error, file lock contention, disk failure, or user cancellation occurs during Phase 1 or Phase 2, all backed-up volumes are restored to their original filenames immediately.
  - Staged temporary parts are cleaned up, leaving original multi-part archives 100% intact and undamaged.
- **Trailing Volume Cleanup (Archive Shrinking)**:
  - Automatically identifies and prunes stale trailing volume files when modifications (e.g. deleting large files) reduce total volume count (e.g., shrinking from 5 parts down to 3 parts).
- **Volume Expansion (Archive Growing)**:
  - Seamlessly handles volume count expansion (e.g., from 2 parts to 4 parts) when additions exceed existing volume capacity.

### Core Modification Pipeline (`core/src/modification.rs`, `core/src/archive.rs`, `core/src/rar.rs`)
- **Multi-Volume 7Z and RAR5 Support**:
  - `Archive::open()` marks multi-volume 7Z (`.7z.001`) and RAR5 (`.part1.rar`) archives as `writable = true`.
  - Modification operations (Add, Delete, Rename, Replace) detect multi-volume archives via `crate::volumes::first_part()` and route execution through the staged split rebuild pipeline.
  - Staging builds the new archive content into a temporary container, re-splits it to match the original volume part size, and invokes `atomic_swap_volumes()`.
  - Preserves original solid block configurations, compression levels, and encryption settings.

### GUI, CLI & FFI Integration
- **FFI Bridge (`core/src/ffi.rs`)**:
  - `archive_writable` queries `Archive::writable()` and returns `true` for split 7Z and RAR archives.
- **GUI Integration (`apps/gui/MainWindow.cpp`, `apps/gui/ArchiveModel.cpp`)**:
  - Action buttons (`Add`, `Delete`, `Rename`, `Replace`) and context menu items are enabled for split archives based on `archive_writable`.
  - Presentation tests verify that split archives present editable action states.
- **CLI Commands (`apps/cli/src/main.rs`, `apps/cli/tests/worker.rs`)**:
  - `add`, `delete`, and `rename` commands execute seamlessly on `.7z.001` and `.part1.rar` multi-volume sets.
  - Supervised worker isolation verifies multi-volume modification under bounded memory limits.

### Inno Setup Installer (`installer/VynxArc.iss`)
- Defined `#define MyAppVersion "1.0.9"`.
- Set `AppVersion={#MyAppVersion}`.
- Updated setup welcome messages in English, Ukrainian, and Russian to announce v1.0.9 and multi-volume archive modification capabilities.

---

## 2. Verification and Qualification Summary

| Verification Area | Scope & Tool | Result |
| :--- | :--- | :--- |
| **Rust Source Formatting** | `cargo fmt --all -- --check` | **Passed** (Clean) |
| **Rust Linting** | `cargo clippy --workspace --all-targets -- -D warnings` | **Passed** (0 warnings) |
| **Rust Workspace Tests** | `cargo test --workspace` | **Passed** (118 passed, 0 failed, 1 ignored) |
| &nbsp;&nbsp;• `core::split_rebuild` | 3-phase swap, preflight margin, shrinking, growing, lock rollback, cancellation | Passed |
| &nbsp;&nbsp;• `core::multivolume_modify` | Add, delete, rename, replace, writable state for split 7Z and RAR5 | Passed |
| &nbsp;&nbsp;• `cli::worker` | CLI split archive modification under supervised worker isolation | Passed |
| **C++ / Qt Build** | CMake 3.x + Ninja + MSVC `cl` (C++20 Release) | **Passed** (Clean build) |
| **CTest Test Suites** | `ctest --test-dir build-msvc --output-on-failure` | **5/5 Passed** (100% pass rate) |
| &nbsp;&nbsp;• `presentation` | Multi-volume writable state, button enablement, accessibility | Passed (1.41s) |
| &nbsp;&nbsp;• `shell_smoke` | Windows Shell COM integration & CLI commands | Passed (2.02s) |
| &nbsp;&nbsp;• `gui_smoke` | GUI startup, menu bar, tabs, window layout | Passed (3.77s) |
| &nbsp;&nbsp;• `gui_batch` | Batch extraction and queue operations | Passed (1.24s) |
| &nbsp;&nbsp;• `gui_product` | Working copies, editor integration, journals | Passed (5.95s) |

---

## 3. Multi-Volume Modification Test Matrix

| # | Test Scenario | Verification Mechanism | Status |
| :- | :--- | :--- | :--- |
| 1 | Consecutive 7Z part enumeration (`.001`..`.003`) | `core/tests/split_rebuild.rs::test_existing_volume_parts_7z` | **Passed** |
| 2 | Consecutive RAR5 part enumeration (`.part1.rar`..`.part3.rar`) | `core/tests/split_rebuild.rs::test_existing_volume_parts_rar_fixtures` | **Passed** |
| 3 | Split volume size detection from initial volume | `core/tests/split_rebuild.rs::test_detect_volume_size` | **Passed** |
| 4 | Preflight disk space verification with 15% safety margin | `core/tests/split_rebuild.rs::test_preflight_multivolume_space` | **Passed** |
| 5 | Standard 3-phase atomic swap (backup -> swap -> purge) | `core/tests/split_rebuild.rs::test_atomic_swap_normal` | **Passed** |
| 6 | Volume count reduction (shrinking) and trailing volume pruning | `core/tests/split_rebuild.rs::test_atomic_swap_shrinking` | **Passed** |
| 7 | Volume count expansion (growing) with new volume placement | `core/tests/split_rebuild.rs::test_atomic_swap_growing` | **Passed** |
| 8 | Rollback on locked volume in Phase 1 (restores original volumes) | `core/tests/split_rebuild.rs::test_atomic_swap_rollback_on_lock` | **Passed** |
| 9 | Rollback on user cancellation signal during swap | `core/tests/split_rebuild.rs::test_atomic_swap_cancellation` | **Passed** |
| 10 | Rollback on Phase 2 placement failure (restores backup chain) | `core/tests/split_rebuild.rs::test_atomic_swap_rollback_on_phase2_failure` | **Passed** |
| 11 | Enumeration termination on non-consecutive volume gaps | `core/tests/split_rebuild.rs::test_gap_in_consecutive_parts` | **Passed** |
| 12 | 4-digit 7Z volume extension format support (`.0001`..`.0004`) | `core/tests/split_rebuild.rs::test_four_digit_7z_parts` | **Passed** |
| 13 | Atomic swap argument validation (empty parts, non-first path) | `core/tests/split_rebuild.rs::test_atomic_swap_invalid_arguments` | **Passed** |
| 14 | Add file to multi-volume 7Z archive (`.7z.001`) with rebuild | `core/tests/multivolume_modify.rs::test_multivolume_7z_add` | **Passed** |
| 15 | Delete file from multi-volume 7Z with volume shrinking & pruning | `core/tests/multivolume_modify.rs::test_multivolume_7z_delete_shrinking` | **Passed** |
| 16 | Rename entry within multi-volume 7Z archive | `core/tests/multivolume_modify.rs::test_multivolume_7z_rename` | **Passed** |
| 17 | Replace file content with larger payload in multi-volume 7Z | `core/tests/multivolume_modify.rs::test_multivolume_7z_replace` | **Passed** |
| 18 | Multi-volume RAR5 reports writable in `Archive::open` | `core/tests/multivolume_modify.rs::test_multivolume_rar_reports_writable` | **Passed** |
| 19 | CLI worker execution of multi-volume 7Z modification | `apps/cli/tests/worker.rs::test_cli_multivolume_7z_modification` | **Passed** |
| 20 | GUI presentation test for split archive writable action states | `apps/gui/tests/GuiPresentationTest.cpp` | **Passed** |

---

## 4. Operational Boundaries and Guarantees

1. **Transactional Integrity**: Multi-volume archives are never modified in-place. All new volume parts are staged in a private temporary directory until completely compressed, formatted, and verified.
2. **Crash & Lock Resilience**: In the event of process termination, disk space depletion, or file locking, original archive parts remain untouched (or are automatically restored from backup).
3. **Volume Split Consistency**: Staged rebuild preserves the volume size configured in the original archive set unless explicitly altered by the user.
4. **Safety Margin**: Disk space exhaustion checks strictly enforce a 15% safety buffer above `old_archive_size + new_archive_size` to prevent drive saturation on shared partitions.
