# VYNX ARC 1.0.0 Release and GitHub Auto-Update Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Release production version 1.0.0 of VYNX ARC with automated GitHub Actions CI/CD for both Portable and Inno Setup packages, and implement an in-app update detector that notifies users of new GitHub releases and updates the application with one click.

**Architecture:** 
1. CI/CD workflow in `.github/workflows/release.yml` triggers on release tags/pushes, compiles the release binaries, packages Inno Setup (`VYNX-ARC-Setup-x64.exe`), Portable (`VYNX-ARC-Portable-x64.zip`), and MSIX, generates `SHA256SUMS.txt`, and publishes a GitHub Release.
2. In-app update subsystem (`UpdateService` in C++/Qt using native WinHTTP or Qt Network) queries the GitHub Releases API (`/repos/Maks0101aps/VYNX-ARC/releases/latest`), parses semver tags, verifies SHA-256 against release checksums, and surfaces a clean non-intrusive notification banner in `MainWindow`.
3. An execution handler downloads the package into `%TEMP%`, verifies its digest, and launches either silent Inno Setup upgrade (`/SILENT /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS`) or an atomic portable replacer script that relaunches `VynxArc.exe`.

**Tech Stack:** C++20, Qt 6 Widgets, Rust 1.99, Inno Setup 6, WinHTTP (Windows native), GitHub Actions, GitHub Releases API.

**Spec:** [docs/ACCEPTANCE_0_9.md](../../ACCEPTANCE_0_9.md), [docs/SUPPORTED_FORMATS.md](../../SUPPORTED_FORMATS.md), [installer/VynxArc.iss](../../../installer/VynxArc.iss).

## Global Constraints

- Windows 11 / 10 x64 compatibility.
- Privacy & Offline default: Update check must be configurable in Settings (Enabled/Disabled, default enabled with check on launch or manual check in Help menu).
- Zero data loss: Update must never overwrite running sessions without confirmation or discard active working copies.
- Integrity first: Any downloaded binary must strictly match the SHA-256 published in `SHA256SUMS.txt` before execution.

## Review Focus

1. Rate limits on GitHub API unauthenticated requests (max 60 requests/hour per IP): cache check timestamp, only check once every 24 hours automatically unless user manually clicks "Check for updates".
2. Portable vs Installed mode detection: check `portable.flag` next to executable; installed uses Inno Setup update, portable uses archive replacement.
3. Locked file replacement on Windows: Inno Setup handles closing and restarting installed app; portable updater must wait for PID exit before overwriting DLLs.
4. User working with unsaved archives: prevent auto-update restart if `hasUnsavedEdits()` is true.
5. Bad network or corrupted download: abort safely and retain existing binaries without corruption.

---

### Task 1: Version Bump to 1.0.0

**Files:**
- Modify: `CMakeLists.txt:2`
- Modify: `core/Cargo.toml`
- Modify: `apps/cli/Cargo.toml`
- Modify: `installer/VynxArc.iss:10`
- Modify: `installer/identity/AppxManifest.xml:10`
- Modify: `docs/CHANGELOG.md`

**Interfaces:**
- Produces: Uniform version `1.0.0` across all metadata, PEs, and installer.

- [ ] **Step 1: Update CMakeLists.txt and Cargo.toml versions**
Update version from `0.9.0` to `1.0.0` in `CMakeLists.txt`, `core/Cargo.toml`, and `apps/cli/Cargo.toml`.

- [ ] **Step 2: Update Inno Setup and MSIX manifests**
Update `AppVersion=1.0.0` in `installer/VynxArc.iss` and `Version="1.0.0.0"` in `installer/identity/AppxManifest.xml`.

- [ ] **Step 3: Update CHANGELOG.md**
Document 1.0.0 release notes with RAR5, ReplaceFileW safety, accessibility, and auto-updater.

- [ ] **Step 4: Verify version consistency**
Run `cargo check` and verify `vynxarc-cli --version` prints `1.0.0`.

- [ ] **Step 5: Commit**
```bash
git add CMakeLists.txt core/Cargo.toml apps/cli/Cargo.toml installer/ docs/CHANGELOG.md
git commit -m "chore: bump version to 1.0.0"
```

---

### Task 2: GitHub Actions Automated CI/CD Release Pipeline

**Files:**
- Modify: `.github/workflows/windows.yml`
- Create: `.github/workflows/release.yml`

**Interfaces:**
- Produces: GitHub Release with attached `VYNX-ARC-Setup-x64.exe`, `VYNX-ARC-Portable-x64.zip`, `VYNX-ARC-Explorer-x64.msix`, and `SHA256SUMS.txt`.

- [ ] **Step 1: Add Inno Setup and full release build to CI**
Install Inno Setup in GitHub Actions runner using `choco install innosetup --no-progress`.
Run `scripts/build-release.ps1 -QtRoot $env:QT_ROOT_DIR` (without `-SkipInstaller`) so both Portable ZIP and Setup EXE are built and validated.

- [ ] **Step 2: Create release publishing workflow**
Create `.github/workflows/release.yml` triggering on tag push `v*`:
- Runs full qualification tests (`scripts/check.ps1`).
- Packages Portable, Setup, MSIX, and SHA256SUMS.txt.
- Uses `softprops/action-gh-release@v2` with `files: dist/1.0.0/*` and auto-generated release notes.

- [ ] **Step 3: Test workflow syntax and configuration**
Verify YAML syntax and ensure token permissions include `contents: write`.

- [ ] **Step 4: Commit**
```bash
git add .github/workflows/
git commit -m "ci: automate GitHub release packaging and publishing for tags"
```

---

### Task 3: In-App Update Service (`UpdateService`)

**Files:**
- Create: `apps/gui/UpdateService.h`
- Create: `apps/gui/UpdateService.cpp`
- Modify: `CMakeLists.txt` (link Qt Network or compile native WinHTTP client)

**Interfaces:**
- Consumes: GitHub API endpoint `https://api.github.com/repos/Maks0101aps/VYNX-ARC/releases/latest`
- Produces:
  ```cpp
  struct ReleaseInfo {
      QString version;
      QString changelog;
      QString setupDownloadUrl;
      QString portableDownloadUrl;
      QString sha256SumsUrl;
      bool isNewerThanCurrent;
  };
  class UpdateService : public QObject {
      Q_OBJECT
  public:
      void checkForUpdates(bool interactive = false);
      void downloadAndInstall(const ReleaseInfo &release, bool isPortable);
  signals:
      void updateAvailable(const ReleaseInfo &release);
      void downloadProgress(qint64 received, qint64 total);
      void updateReady(const QString &installerPath);
      void updateError(const QString &errorMessage);
  };
  ```

- [ ] **Step 1: Implement GitHub release parser and semver comparison**
Write unit test in `apps/gui/tests/` for parsing GitHub Release JSON payload and comparing `v1.0.1` vs `1.0.0`.

- [ ] **Step 2: Implement asynchronous HTTP query**
Use Qt Network or Windows WinHTTP to query GitHub API with custom User-Agent `VYNX-ARC-Updater/1.0.0`. Handle rate-limiting (403) and offline status gracefully without UI blocking.

- [ ] **Step 3: Implement SHA-256 validation of downloaded asset**
Download `SHA256SUMS.txt` alongside the installer/zip, parse expected hash for the file, and compute SHA-256 before signaling `updateReady`. Reject if hash mismatches.

- [ ] **Step 4: Commit**
```bash
git add apps/gui/UpdateService.* CMakeLists.txt
git commit -m "feat(gui): implement UpdateService with SHA256 verification"
```

---

### Task 4: GUI Notification and One-Click Update UX

**Files:**
- Create: `apps/gui/dialogs/UpdateDialog.h`
- Create: `apps/gui/dialogs/UpdateDialog.cpp`
- Modify: `apps/gui/MainWindow.h`
- Modify: `apps/gui/MainWindow.cpp`
- Modify: `apps/gui/Theme.cpp` (styling for update banner)

**Interfaces:**
- Produces: Discreet notification banner in MainWindow ("Доступна новая версия v1.0.1. [Обновить]") and interactive Update Dialog.

- [ ] **Step 1: Create top-bar update notification banner**
In `MainWindow`, add an animated or slide-down banner `QFrame#updateBanner` below the main menu:
- Text: "Доступна версия 1.0.1" / "Version 1.0.1 is available"
- Buttons: "Обновить" (Update), "Что нового" (Changelog), "✕" (Dismiss).

- [ ] **Step 2: Create `UpdateDialog`**
Modal or modeless dialog showing:
- Current version vs New version.
- Markdown/plaintext formatted release notes from GitHub.
- Download progress bar with bytes transferred and speed.
- Action button: "Установить и перезапустить" (Install & Restart).

- [ ] **Step 3: Connect menu item "Справка -> Проверить обновления"**
In `MainWindow::createMenus()`, add `actionCheckUpdates` with shortcut and manual trigger.

- [ ] **Step 4: Commit**
```bash
git add apps/gui/dialogs/UpdateDialog.* apps/gui/MainWindow* apps/gui/Theme.cpp
git commit -m "feat(gui): add update notification banner and dialog"
```

---

### Task 5: Safe Application Upgrade and Restart Execution

**Files:**
- Create: `apps/gui/UpdateInstaller.h`
- Create: `apps/gui/UpdateInstaller.cpp`

**Interfaces:**
- Consumes: Validated executable or zip path in `%TEMP%`, `isPortable` boolean.
- Produces: Self-terminating relaunch sequence.

- [ ] **Step 1: Implement Inno Setup silent execution (Installed mode)**
If installed mode:
Check if user has unsaved edits in open tabs. If not:
Execute:
`vynx-setup.exe /SILENT /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS`
Exit `VynxArc.exe` cleanly. Inno Setup waits for process termination, overwrites files in `{localappdata}\Programs\VYNX ARC`, and restarts the application.

- [ ] **Step 2: Implement Portable batch replacement (Portable mode)**
If `portable.flag` exists:
Create a small helper batch file `update-portable.bat` in `%TEMP%`:
```cmd
@echo off
timeout /t 1 /nobreak >nul
tar -xf "%TEMP%\VYNX-ARC-Portable-x64.zip" -C "%APP_DIR%"
start "" "%APP_DIR%\VynxArc.exe"
del "%~f0"
```
Launch detached and terminate `VynxArc.exe`.

- [ ] **Step 3: Verify and handle failure scenarios**
If update process fails to launch, report descriptive error and do not close the current window.

- [ ] **Step 4: Commit**
```bash
git add apps/gui/UpdateInstaller.*
git commit -m "feat(gui): implement safe updater launcher for setup and portable modes"
```

---

### Task 6: Final End-to-End Release Acceptance

**Files:**
- Modify: `docs/ACCEPTANCE_1_0.md`
- Run: `scripts/check.ps1`
- Run: `scripts/build-release.ps1`

- [ ] **Step 1: Run complete check script**
Execute `scripts/check.ps1` (format, Clippy, 110+ Rust tests, audit, CMake, CTest).

- [ ] **Step 2: Build release packages**
Execute `scripts/build-release.ps1` to generate:
- `dist/1.0.0/VYNX-ARC-Portable-x64.zip`
- `dist/1.0.0/VYNX-ARC-Setup-x64.exe`
- `dist/1.0.0/VYNX-ARC-Explorer-x64.msix`
- `dist/1.0.0/SHA256SUMS.txt`

- [ ] **Step 3: Document Acceptance 1.0.0**
Write `docs/ACCEPTANCE_1_0.md` detailing all verified test cases, updater qualification, and artifact SHA-256 hashes.

- [ ] **Step 4: Tag and Push Release**
```bash
git tag -a v1.0.0 -m "Release 1.0.0: Native RAR5, supervised workers, and auto-update"
git push origin v1.0.0
```
Verify GitHub Actions workflow executes, publishes release, and attaches binaries.
