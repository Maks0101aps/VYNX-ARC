# Multi-Volume Archive Modification & Staged Rebuild Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement safe, transactional multi-volume archive modification (Add, Delete, Rename, Replace) with atomic staging and two-phase rollback for `.7z.001` and `.part1.rar` archives in VYNX ARC v1.0.9.

**Architecture:** A transactional split-rebuild engine (`core/src/split_rebuild.rs`) preflights disk space, streams new modified archive contents into a temporary split set (`.001.tmp`..`.NNN.tmp`), verifies volume headers, and performs a two-phase atomic swap (backup originals -> move new parts -> purge backups) with fail-safe rollback to prevent volume chain corruption.

**Tech Stack:** Rust 2024, CXX FFI, C++20, Qt 6 Widgets, Windows NTFS atomic rename APIs (`MoveFileExW` with `MOVEFILE_REPLACE_EXISTING`).

**Spec:** [docs/ROADMAP_POST_1_0.md](file:///c:/Users/Maksi/Documents/projects/VYNX-ARC/docs/ROADMAP_POST_1_0.md)

## Global Constraints

- Windows 10/11 x64 target platform; MSVC toolchain.
- Strict fail-closed security: never touch original volumes until the entire new multi-volume chain is fully written, flushed, and verified.
- Atomic rollback: if any error or cancellation occurs before the final commit, all original parts are restored and all temporary staging files deleted.
- Dynamic volume resizing: properly handles volume count shrinking (e.g. 5 parts -> 3 parts) or growing (3 parts -> 5 parts), removing stale trailing volumes.
- All code must pass `cargo test --workspace`, strict Clippy (`-D warnings`), and CTest.

## Review Focus

1. **Preflight Disk Space Exhaustion:** Must calculate free disk space on the target volume ensuring capacity for old archive + new archive + 15% safety buffer before starting rebuild.
2. **Atomic Swap & Rollback on Locked Volume:** If one of the original volumes is locked by an external process during commit, must restore all `.vynx-bak` volumes and clean up staging without losing data.
3. **Volume Count Reduction (Shrinking):** When files are deleted from a 5-volume archive reducing it to 3 volumes, old volumes `.004` and `.005` must be safely pruned.
4. **Volume Header & Chain Verification:** Rebuilt multi-volume parts must contain valid 7Z/RAR signatures, sequence numbers, and block offsets verifiable by standard unarchivers.
5. **Cancellation Mid-Stream:** Cancelling the operation during split re-compression must immediately clean up temporary files without altering original volumes.

---

### Task 1: Transactional Split Staging & Two-Phase Swap Engine

**Files:**
- Create: `core/src/split_rebuild.rs`
- Modify: `core/src/volumes.rs`
- Modify: `core/src/lib.rs`
- Test: `core/tests/split_rebuild.rs`

**Interfaces:**
- Consumes: `crate::ArcError`, `crate::Operation`, `crate::Result`, `crate::security`, `crate::disk`
- Produces:
  - `pub fn existing_volume_parts(first: &Path) -> Result<Vec<PathBuf>>`
  - `pub fn detect_volume_size(first: &Path) -> Result<u64>`
  - `pub fn preflight_multivolume_space(first: &Path, needed_bytes: u64) -> Result<()>`
  - `pub fn atomic_swap_volumes(first: &Path, staged_parts: &[PathBuf], op: &Operation) -> Result<()>`

- [ ] **Step 1: Write integration test in `core/tests/split_rebuild.rs`**

Test staging new volume parts, executing `atomic_swap_volumes()`, verifying that old parts are replaced, old trailing parts are removed, and test simulated failure triggering automatic rollback to original parts.

- [ ] **Step 2: Run test to verify it fails**

Run: `cargo test --test split_rebuild`
Expected: FAIL with "unresolved import vynx_arc_core::split_rebuild"

- [ ] **Step 3: Implement `core/src/split_rebuild.rs` and export module in `core/src/lib.rs`**

- Implement `existing_volume_parts(first)` listing `.001`..`.NNN` or `.part1.rar`..`.partN.rar`.
- Implement `detect_volume_size(first)` reading size of part 1.
- Implement `preflight_multivolume_space(first, needed_bytes)`.
- Implement `atomic_swap_volumes(first, staged_parts, op)` with 3-phase execution:
  - Phase 1: Rename old parts to `.vynx-bak-<uuid>`
  - Phase 2: Move staged parts to target names
  - Phase 3: Delete backups
  - On error at Phase 1 or 2: rollback backups to original names.

- [ ] **Step 4: Run test to verify it passes**

Run: `cargo test --test split_rebuild`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add core/src/split_rebuild.rs core/src/volumes.rs core/src/lib.rs core/tests/split_rebuild.rs
git commit -m "feat(core): implement transactional multi-volume split staging and two-phase swap engine"
```

---

### Task 2: Core Modification Integration for Multi-Volume 7Z & RAR

**Files:**
- Modify: `core/src/archive.rs`
- Modify: `core/src/modification.rs`
- Modify: `core/src/rar.rs`
- Modify: `core/src/operations.rs`
- Test: `core/tests/multivolume_modify.rs`

**Interfaces:**
- Consumes: `core/src/split_rebuild.rs`
- Produces:
  - `Archive::open()` marks multi-volume 7Z and RAR archives as `writable = true`
  - `modification::modify()` routes multi-volume archives to split rebuild pipeline
  - Full support for Add, Delete, Rename, Replace on `.7z.001` and `.part1.rar`

- [ ] **Step 1: Write integration test in `core/tests/multivolume_modify.rs`**

Create a real 3-part split `.7z.001` archive, call `modification::modify()` to add a new file and delete an existing file, verify all parts are updated, verify parts can be opened and read by `Archive::open()` and `Reader::open()`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cargo test --test multivolume_modify`
Expected: FAIL with `READ_ONLY` error

- [ ] **Step 3: Implement multi-volume modification pipeline in `modification.rs` and `archive.rs`**

- In `core/src/archive.rs`: allow `writable = true` when `format == Format::SevenZ` even if `first_part(&path).is_some()`.
- In `core/src/rar.rs`: allow `writable = true` for multi-volume RAR if writer features are available.
- In `core/src/modification.rs`:
  - When `crate::volumes::first_part(&archive.path).is_some()`:
    - Preflight disk space for multi-volume staging.
    - Rebuild content into temporary archive.
    - Split temporary archive into staged volume files matching original volume size.
    - Execute `atomic_swap_volumes()`.
- Update `operations.rs` to handle receipt and digest verification.

- [ ] **Step 4: Run test to verify it passes**

Run: `cargo test --test multivolume_modify && cargo test --workspace`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add core/src/archive.rs core/src/modification.rs core/src/rar.rs core/src/operations.rs core/tests/multivolume_modify.rs
git commit -m "feat(core): enable safe modification and staged rebuild for multi-volume archives"
```

---

### Task 3: GUI, CLI & FFI Integration for Multi-Volume Modification

**Files:**
- Modify: `core/src/ffi.rs`
- Modify: `apps/gui/ArchiveModel.cpp`
- Modify: `apps/gui/MainWindow.cpp`
- Modify: `apps/cli/src/main.rs`
- Test: `apps/gui/tests/GuiPresentationTest.cpp`

**Interfaces:**
- Consumes: `core/src/modification.rs`
- Produces:
  - FFI `archive_writable` returns `true` for multi-volume 7Z and RAR
  - GUI enables modification action buttons (Add, Delete, Rename, Replace) for split archives
  - CLI `add`, `delete`, `rename` commands execute seamlessly on `.7z.001` and `.part1.rar`

- [ ] **Step 1: Add failing GUI presentation test in `GuiPresentationTest.cpp`**

Assert that multi-volume 7Z archives report writable status and allow edit actions.

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest -R presentation --output-on-failure`
Expected: FAIL

- [ ] **Step 3: Update GUI and CLI code**

- In `apps/gui/MainWindow.cpp` and `ArchiveModel.cpp`: enable edit actions and context menu items when `archive_writable()` is true on split archives.
- In `apps/cli/src/main.rs`: update help text and ensure `add`, `delete`, `rename` operate cleanly on `.7z.001` and `.part1.rar`.

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest -R presentation --output-on-failure && cargo test --workspace`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add core/src/ffi.rs apps/gui/ArchiveModel.cpp apps/gui/MainWindow.cpp apps/cli/src/main.rs apps/gui/tests/GuiPresentationTest.cpp
git commit -m "feat(ui): enable multi-volume archive modification in GUI and CLI"
```

---

### Task 4: Inno Setup, Version Bump to 1.0.9 & Release Acceptance

**Files:**
- Modify: `Cargo.toml`
- Modify: `core/Cargo.toml`
- Modify: `apps/cli/Cargo.toml`
- Modify: `Cargo.lock`
- Modify: `CMakeLists.txt`
- Modify: `installer/VynxArc.iss`
- Modify: `docs/CHANGELOG.md`
- Create: `docs/ACCEPTANCE_1_0_9.md`

- [ ] **Step 1: Bump version to 1.0.9 across all manifests**

Update version from `1.0.5` to `1.0.9` in `core/Cargo.toml`, `apps/cli/Cargo.toml`, `Cargo.lock`, `CMakeLists.txt`, and `installer/VynxArc.iss`.

- [ ] **Step 2: Update Inno Setup script**

Update setup welcome messages in English, Ukrainian, and Russian to announce v1.0.9 and Atomic Split Matrix multi-volume modification capabilities.

- [ ] **Step 3: Run full verification test suite**

Run: `cargo test --workspace && ctest --output-on-failure`
Expected: All tests pass with zero failures.

- [ ] **Step 4: Update CHANGELOG.md and create docs/ACCEPTANCE_1_0_9.md**

Document v1.0.9 release highlights, Atomic Split Matrix specifications, test evidence, and verification logs.

- [ ] **Step 5: Commit**

```bash
git add installer/VynxArc.iss Cargo.toml core/Cargo.toml apps/cli/Cargo.toml Cargo.lock CMakeLists.txt docs/CHANGELOG.md docs/ACCEPTANCE_1_0_9.md
git commit -m "release: v1.0.9 with Atomic Split Matrix multi-volume archive modification"
```
