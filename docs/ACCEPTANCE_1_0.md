# 1.0.0 Release Acceptance and Qualification Report

Date: 2026-11-21. Target Version: **1.0.0 GA**.
Base commit chain:
- `c51747f`: chore: bump version to 1.0.0
- `d69bd78`: ci: add automated GitHub Actions release workflow
- `87a3794`: feat(gui): implement UpdateService with WinHTTP and SHA-256 validation
- `a9eac23`: feat(gui): add update notification banner and UpdateDialog in MainWindow
- `c6f2fbf`: feat(gui): implement safe updater launcher and restart execution

Branch: `feature/1.0.0-release-and-autoupdate`.

---

## 1. Implemented Scope

The 1.0.0 release concludes the initial production-ready milestone for VYNX ARC. It delivers full archive management capabilities, automated release builds, and an integrated in-app auto-update system.

### Automated Release Pipeline (`.github/workflows/release.yml`)
- Triggered automatically on Git tag pushes matching `v*` and manual `workflow_dispatch`.
- Builds on `windows-latest` with pinned Rust MSVC toolchain, Qt 6.12.0 SDK, Visual Studio 2022 C++20 Build Tools, and Inno Setup 6.
- Executes complete verification pipeline (`scripts/build-release.ps1`):
  - Rust formatting, Clippy warnings check, locked test suite, and cargo audit.
  - C++20 / Qt 6 build with CMake and Ninja.
  - CTest verification suites (presentation, shell smoke, GUI smoke, GUI batch, GUI product).
  - Windows SDK package creation (Inno Setup Installer, Portable ZIP, MSIX sparse package).
  - SHA-256 checksum generation into `SHA256SUMS.txt`.
- Publishes all generated release assets directly to GitHub Releases via `softprops/action-gh-release@v2`.

### In-App Auto-Updater (`UpdateService`, `UpdateDialog`, `UpdateInstaller`)
- **Native Network Layer (`UpdateService`)**:
  - Implemented with WinHTTP on Windows, avoiding third-party network runtime dependencies.
  - Asynchronously queries the GitHub Releases API (`https://api.github.com/repos/Maks0101aps/VYNX-ARC/releases/latest`) on startup and via **Help > Check for Updates...**.
  - Robust semantic version comparison handles major, minor, patch, and prerelease versions.
  - Automatic download and parsing of `SHA256SUMS.txt` over TLS.
  - Performs SHA-256 integrity validation on downloaded installer/portable packages prior to execution; rejects corrupted or tampered downloads.
- **User Interface (`UpdateDialog`, MainWindow Banner)**:
  - Non-intrusive notification banner in `MainWindow` with one-click "Update Now" and "Dismiss" actions.
  - Dedicated modal `UpdateDialog` displaying version information, formatted release notes, download progress bar, and installation trigger.
  - Conforms to accessibility standards with complete keyboard navigation, label associations, and high-DPI scaling.
- **Process Orchestration (`UpdateInstaller`)**:
  - Automatically identifies execution mode (Installed via Inno Setup vs. Portable via `portable.flag`).
  - **Installed Mode**: Launches `VYNX-ARC-Setup-x64.exe` with `/SILENT /NORESTART` parameters, safely exiting the current application.
  - **Portable Mode**: Generates a self-contained batch update script (`update-vynx.bat`) in `%TEMP%`, passes parent PID, waits for graceful application termination, extracts files from `VYNX-ARC-Portable-x64.zip`, replaces binaries, and relaunches `VynxArc.exe`.

### Dependency Security & Crate Pinning
- Upgraded `zstd` from 0.14.2 to pinned `=0.14.4` (along with `zstd-safe` 8.3.0 and `zstd-sys` 2.1.3), resolving security advisories `RUSTSEC-2026-0391`, `RUSTSEC-2026-0398`, `RUSTSEC-2026-0403`, and `RUSTSEC-2026-0410`.
- Cargo audit passes cleanly with 0 vulnerabilities and 1 allowed warning (rustix unsound warning).

---

## 2. Verification and Qualification Summary

| Verification Area | Scope & Tool | Result |
| :--- | :--- | :--- |
| **Rust Source Formatting** | `cargo fmt --all -- --check` | **Passed** (Clean) |
| **Rust Linting** | `cargo clippy --workspace --all-targets --locked -- -D warnings` | **Passed** (0 warnings) |
| **Rust Test Suite** | `cargo test --workspace --locked` | **Passed** (90 passed, 0 failed, 1 ignored) |
| **Dependency Security** | `cargo audit --deny yanked` (1555 advisories scanned) | **Passed** (0 vulnerabilities) |
| **C++ / Qt Build** | CMake 3.x + Ninja + MSVC `cl` (C++20 Release) | **Passed** (Clean build) |
| **CTest Test Suites** | `ctest --test-dir build-msvc --output-on-failure` | **5/5 Passed** (100% pass rate) |
| &nbsp;&nbsp;• `presentation` | GUI accessibility, labels, table read-only | Passed (1.15s) |
| &nbsp;&nbsp;• `shell_smoke` | Windows Shell integration & CLI isolated commands | Passed (2.04s) |
| &nbsp;&nbsp;• `gui_smoke` | GUI startup, menu bar, tabs, window layout | Passed (4.14s) |
| &nbsp;&nbsp;• `gui_batch` | Batch extraction and queue operations | Passed (1.23s) |
| &nbsp;&nbsp;• `gui_product` | Working copies, editor integration, journals | Passed (5.46s) |
| **CLI Verification** | `vynxarc-cli.exe --help` | **Passed** (Command table verified) |
| **GUI Smoke Test** | `VynxArc.exe --smoke-test` | **Passed** (Clean headless launch & exit) |
| **Native RAR & WinRAR 7.22** | `scripts/check-rar-native.py` with WinRAR 7.22 | **21/21 Passed** (All RAR cases verified) |

### WinRAR 21/21 Matrix Details
The native RAR5 creation and extraction engine was qualified against official WinRAR 7.22 across 21 test matrices:
1. Fast compression preset
2. Balanced compression preset
3. Maximum compression preset
4. Store (uncompressed) mode
5. Solid archive creation & extraction
6. Custom dictionary size (128 KB - 64 MB)
7. AES-256 payload encryption
8. AES-256 encrypted headers (hidden filenames)
9. Unicode filenames and long Windows paths
10. Empty files and nested directories
11. Multivolume split archive generation
12. Single-volume verified rebuilds (add file)
13. Single-volume verified rebuilds (delete file)
14. Single-volume verified rebuilds (rename file)
15. Recovery record generation (3% - 10%)
16. Recovery record repair of corrupted payload
17. Rejection of corrupted headers
18. Rejection of invalid passwords
19. Rejection of missing volume parts
20. Resource-constrained Eco mode budget
21. High-throughput sequential write

---

## 3. Official Release Artifacts (1.0.0)

Packages built in `dist/1.0.0/` using `scripts/build-release.ps1`:

| Artifact | Size (Bytes) | SHA-256 Checksum |
| :--- | :--- | :--- |
| **`VYNX-ARC-Setup-x64.exe`** | 18,879,052 | `af5f1a551ec6a8ea5adc93d2ec4ab00a30e95d2e6dc4fc4aa209fb6d9711af5b` |
| **`VYNX-ARC-Portable-x64.zip`** | 26,933,552 | `ef657842d5c5ce63c9467122030630d3fc50bb2941515e285aa1c7bdcd901aef` |
| **`VYNX-ARC-Explorer-x64.msix`** | 13,180 | `939b4b133adb6add14190dde44591516089f176398c7eb7e73881a6919ebbb5f` |
| **`SHA256SUMS.txt`** | 277 | Included in release package directory |

### `SHA256SUMS.txt` Content
```text
939b4b133adb6add14190dde44591516089f176398c7eb7e73881a6919ebbb5f  VYNX-ARC-Explorer-x64.msix
ef657842d5c5ce63c9467122030630d3fc50bb2941515e285aa1c7bdcd901aef  VYNX-ARC-Portable-x64.zip
af5f1a551ec6a8ea5adc93d2ec4ab00a30e95d2e6dc4fc4aa209fb6d9711af5b  VYNX-ARC-Setup-x64.exe
```

---

## 4. Distribution Boundaries and Operational Notes

1. **Packaging & Signing**: Release binaries are built with Microsoft Visual Studio 2022 C++20 and the MSVC CRT app-local redistributable. Binaries in local builds are unsigned; official releases are signed via the project's Authenticode certificate pipeline.
2. **Platform Support**: Tested and validated on Windows 10 (x64) and Windows 11 (x64).
3. **Auto-Update Endpoint**: Production updates query `https://api.github.com/repos/Maks0101aps/VYNX-ARC/releases/latest`. Custom endpoints can be configured for enterprise or internal deployments via `UpdateService::checkForUpdates(currentVersion, apiUrl)`.
