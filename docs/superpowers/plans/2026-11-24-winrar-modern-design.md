# Modern WinRAR-Inspired Interface & Vector Asset System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Overhaul the VYNX ARC desktop interface into a modern, neo-classic WinRAR aesthetic featuring a prominent action toolbar with large icons and labels underneath, interactive rubberband / marquee mouse selection ("обводка курсором"), custom SVG vector assets for files and actions, parent folder navigation (`..`), and refined Light and Dark themes with blazing-fast 100K+ file performance.

**Architecture:** A comprehensive GUI modernization centered in `apps/gui`:
1. High-fidelity vector icon system in `assets/icons/ui/` and `IconProvider.cpp` providing modern dual-tone, palette-aware file type icons (`folder`, `folder_up`, `exe`, `txt`, `archive`, `code`, `media`, etc.) and toolbar glyphs.
2. An interactive `QRubberBand` viewport event filter in `QTableView` supporting multi-row marquee selection without performance degradation on large archives.
3. Restructured `ArchivePage` layout featuring an ergonomic WinRAR command toolbar with logical clusters (`Add`, `Extract To`, `Test`, `View`, `Delete`, `Rename` | `Find`, `Wizard`, `Info` | `Verify/Repair`) above a path navigation bar with Up (`↑`) button, breadcrumb address bar, and search.
4. `..` parent folder row in `ArchiveModel` pinned above sorted items.
5. Modernized QSS stylesheets and QPalette configurations in `Theme.cpp` for both Light and Dark themes.

**Tech Stack:** C++20, Qt 6 (Widgets, Gui, Core, Svg), CMake, MSVC toolchain on Windows 10/11 x64.

**Spec:** [docs/superpowers/specs/2026-11-24-winrar-modern-design.md](file:///c:/Users/Maksi/Documents/projects/VYNX-ARC/docs/superpowers/specs/2026-11-24-winrar-modern-design.md)

## Global Constraints

- Target OS: Windows 10/11 x64; MSVC 2022 toolchain; Qt 6 Widgets.
- Zero telemetry, zero external network calls, fail-closed security.
- Blazing-fast performance: marquee selection, icon rendering, and folder navigation must remain silky-smooth and instant on archives with 100,000+ files.
- High-DPI scaling: all icons and layout elements must render crisp and pixel-aligned at 100%, 125%, 150%, and 200% display scaling.
- All code must pass strict compilation, zero warnings on Clippy and MSVC, and 100% CTest pass rate.

## Review Focus

1. **Rubberband Performance on 100K Items:** Dragging the selection rectangle over thousands of items must not trigger O(N) linear scans on every mouse move tick; must use spatial `rowAt()` / `visualRect()` bounding-box queries.
2. **`..` Navigation Row State Consistency:** The `..` row must remain pinned to row 0 regardless of column sorting, must not be selectable by batch operations (Extract/Delete), and must cleanly navigate up one directory level on double click or Enter.
3. **Responsive Toolbar Overflow:** When resizing the window below 780px, secondary tool buttons must fold into the `More...` dropdown without breaking existing shortcuts (`F2`, `F3`, `Del`, `Ctrl+F`).
4. **Theme Transition Visual Integrity:** Switching between Light and Dark themes at runtime must update all toolbar buttons, selection highlights, rubberband transparency, and icon palette tints without needing an application restart.
5. **No Regressions in Existing GUI Tests:** Must maintain full backward compatibility with all existing CTest suites (`presentation`, `shell_smoke`, `gui_smoke`, `gui_batch`, `gui_product`).

---

### Task 1: SVG Vector Asset Suite & Theme-Adaptive Icon Provider

**Files:**
- Create: `assets/icons/ui/tb_add.svg`, `assets/icons/ui/tb_extract.svg`, `assets/icons/ui/tb_test.svg`, `assets/icons/ui/tb_view.svg`, `assets/icons/ui/tb_delete.svg`, `assets/icons/ui/tb_rename.svg`, `assets/icons/ui/tb_find.svg`, `assets/icons/ui/tb_wizard.svg`, `assets/icons/ui/tb_info.svg`, `assets/icons/ui/tb_repair.svg`, `assets/icons/ui/folder_up.svg`, `assets/icons/ui/lock.svg`, `assets/icons/ui/code.svg`, `assets/icons/ui/audio.svg`, `assets/icons/ui/video.svg`, `assets/icons/ui/pdf.svg`, `assets/icons/ui/doc.svg`, `assets/icons/ui/iso.svg`, `assets/icons/ui/wim.svg`
- Modify: `assets/icons/ui/folder.svg`, `assets/icons/ui/executable.svg`, `assets/icons/ui/text.svg`, `assets/icons/ui/archive.svg`
- Modify: `apps/gui/widgets/IconProvider.h`
- Modify: `apps/gui/widgets/IconProvider.cpp`
- Modify: `CMakeLists.txt`
- Test: `apps/gui/tests/GuiPresentationTest.cpp`

**Interfaces:**
- Consumes: Qt 6 `QIcon`, `QPainter`, `QPalette`
- Produces:
  - `QIcon Icons::get(const QString &kind)` supporting all new toolbar kinds (`tb_add`, `tb_extract`, `tb_test`, `tb_view`, `tb_delete`, `tb_rename`, `tb_find`, `tb_wizard`, `tb_info`, `tb_repair`, `up`, `lock`)
  - `QIcon Icons::file(const QString &name, bool folder, bool isUp = false)` returning specialized vector icons for `exe`, `txt`, `archive`, `code`, `audio`, `video`, `pdf`, `doc`, `iso`, `wim`, `folder`, and `folder_up` (`..`).

- [ ] **Step 1: Write failing presentation test in `GuiPresentationTest.cpp`**

```cpp
void vectorIconsCompleteSet() {
    for (const auto &name : {"tb_add", "tb_extract", "tb_test", "tb_view", "tb_delete", 
                             "tb_rename", "tb_find", "tb_wizard", "tb_info", "tb_repair", "lock"}) {
        const auto icon = Icons::get(name);
        QVERIFY(!icon.isNull());
        const auto pixmap = icon.pixmap(32, 32);
        QVERIFY(!pixmap.isNull());
    }
    for (const auto &ext : {"test.exe", "doc.txt", "arch.zip", "script.rs", "track.mp3", "movie.mp4", "doc.pdf", "cd.iso", "sys.wim"}) {
        const auto icon = Icons::file(ext, false);
        QVERIFY(!icon.isNull());
    }
    const auto upIcon = Icons::file("..", true, true);
    QVERIFY(!upIcon.isNull());
}
```

- [ ] **Step 2: Run CTest to verify it fails**

Run: `. .\scripts\env.ps1; ctest --test-dir build-msvc -R presentation --output-on-failure`
Expected: FAIL (missing icon definitions / assertions).

- [ ] **Step 3: Create SVG files and implement enhanced `IconProvider`**

1. Create clean, modern 24x24 / 32x32 SVG vector files in `assets/icons/ui/` with modern dual-tone styling.
2. Add resources to `CMakeLists.txt` using `qt_add_resources`.
3. Update `IconProvider.cpp` with a high-performance `QIconEngine` that renders dual-tone icons adapting to Light and Dark palettes dynamically (e.g. amber for folders, royal blue for archives, emerald for test, crimson for delete, purple for exe).
4. Update `Icons::file()` to map extensions (.rs, .cpp, .py, .js, .mp3, .mp4, .pdf, .docx, .iso, .wim, etc.) to appropriate vector icons.

- [ ] **Step 4: Run CTest to verify it passes**

Run: `. .\scripts\env.ps1; ctest --test-dir build-msvc -R presentation --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add assets/icons/ui/ CMakeLists.txt apps/gui/widgets/IconProvider.h apps/gui/widgets/IconProvider.cpp apps/gui/tests/GuiPresentationTest.cpp
git commit -m "feat(gui): introduce modern dual-tone vector asset suite and theme-adaptive icon provider"
```

---

### Task 2: High-Performance Rubberband / Marquee Selection in File Table

**Files:**
- Create: `apps/gui/widgets/TableRubberBand.h`
- Create: `apps/gui/widgets/TableRubberBand.cpp`
- Modify: `apps/gui/pages/ArchivePage.cpp`
- Modify: `apps/gui/Theme.cpp`
- Test: `apps/gui/tests/GuiPresentationTest.cpp`

**Interfaces:**
- Consumes: `QTableView`, `QItemSelectionModel`, `QRubberBand`
- Produces:
  - `class TableRubberBand : public QObject`: Viewport event filter handling `MouseButtonPress`, `MouseMove`, and `MouseButtonRelease` to draw a semi-transparent selection marquee and batch-select enclosed rows via `QItemSelection`.

- [ ] **Step 1: Write failing test in `GuiPresentationTest.cpp`**

```cpp
void rubberBandSelectionIntersectsRows() {
    // Assert that TableRubberBand selects row 0 and 1 when marquee spans their visual bounding rects
    // and ignores un-intersected rows
}
```

- [ ] **Step 2: Run CTest to verify it fails**

Run: `. .\scripts\env.ps1; ctest --test-dir build-msvc -R presentation --output-on-failure`
Expected: FAIL

- [ ] **Step 3: Implement `TableRubberBand` and attach to `table->viewport()`**

1. Implement `TableRubberBand` in `apps/gui/widgets/TableRubberBand.cpp`:
   - Start drag on left mouse button press in viewport if not directly clicking interactive controls.
   - On `MouseMove`: instantiate and update `QRubberBand(QRubberBand::Rectangle, viewport)`.
   - Compute intersecting rows efficiently: determine top and bottom visible row boundaries via `table->rowAt(rect.top())` and `table->rowAt(rect.bottom())`, avoiding full O(N) table iteration.
   - Construct a `QItemSelection` for the spanned row range and apply with `QItemSelectionModel::Select | QItemSelectionModel::Rows` (honoring `Qt::ControlModifier` for toggle and `Qt::ShiftModifier` for range).
   - On `MouseButtonRelease`: hide rubber band.
2. In `ArchivePage.cpp`: install `TableRubberBand` on `table->viewport()`.
3. In `Theme.cpp`: style `QRubberBand` with `background: rgba(37, 99, 235, 0.22); border: 1px solid #2563eb; border-radius: 2px;` in light theme, and `rgba(59, 130, 246, 0.28)` / `#3b82f6` in dark theme.

- [ ] **Step 4: Run CTest to verify it passes**

Run: `. .\scripts\env.ps1; ctest --test-dir build-msvc -R presentation --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add apps/gui/widgets/TableRubberBand.h apps/gui/widgets/TableRubberBand.cpp apps/gui/pages/ArchivePage.cpp apps/gui/Theme.cpp apps/gui/tests/GuiPresentationTest.cpp
git commit -m "feat(gui): implement interactive rubberband marquee selection for file table"
```

---

### Task 3: Neo-Classic WinRAR Command Toolbar & Sub-Navigation Bar

**Files:**
- Modify: `apps/gui/pages/ArchivePage.h`
- Modify: `apps/gui/pages/ArchivePage.cpp`
- Modify: `apps/gui/MainWindow.cpp`
- Modify: `apps/gui/MainWindow.h`
- Test: `apps/gui/tests/GuiPresentationTest.cpp`

**Interfaces:**
- Consumes: Tasks 1 & 2, `MainWindow` action slots
- Produces:
  - Top action toolbar with large icons and labels underneath (`ToolButtonTextUnderIcon`), divided into 3 logical clusters.
  - Sub-navigation bar with `↑` button, address bar, and quick search box.

- [ ] **Step 1: Write failing test in `GuiPresentationTest.cpp`**

```cpp
void winrarToolbarActionLayout() {
    // Verify that ArchivePage contains primary WinRAR action buttons with ToolButtonTextUnderIcon
    // and navigation bar with up button and search
}
```

- [ ] **Step 2: Run CTest to verify it fails**

Run: `. .\scripts\env.ps1; ctest --test-dir build-msvc -R presentation --output-on-failure`
Expected: FAIL

- [ ] **Step 3: Restructure `ArchivePage` and wire actions in `MainWindow`**

1. In `ArchivePage.cpp`:
   - Build prominent top toolbar layout with `setToolButtonStyle(Qt::ToolButtonTextUnderIcon)` and 32x32 icon size.
   - Cluster 1: `Add`, `Extract To`, `Test`, `View`, `Delete`, `Rename`.
   - Separator.
   - Cluster 2: `Find`, `Wizard / Batch`, `Info`.
   - Separator.
   - Cluster 3: `Repair / Verify`.
   - Setup navigation bar below toolbar: `up` button (`32x32`), `breadcrumb` address bar, `search` field.
2. In `MainWindow.cpp`:
   - Wire `wizard` button to `chooseBatchExtract()`.
   - Wire `info` button to `showProperties()`.
   - Wire `view` button to `previewSelected()`.
   - Wire `find` button to focus the search box.
   - Wire `repair` button to `reviewInterruptedExtraction` / hash verification.
   - Update `ArchivePage::adapt()` responsive folding logic so secondary clusters fold gracefully into `More...` on window width < 780px.

- [ ] **Step 4: Run CTest to verify it passes**

Run: `. .\scripts\env.ps1; ctest --test-dir build-msvc -R presentation --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add apps/gui/pages/ArchivePage.h apps/gui/pages/ArchivePage.cpp apps/gui/MainWindow.h apps/gui/MainWindow.cpp apps/gui/tests/GuiPresentationTest.cpp
git commit -m "feat(gui): restructure archive view with WinRAR action toolbar and sub-navigation bar"
```

---

### Task 4: `..` Parent Folder Navigation Row & Status Bar Encryption Indicator

**Files:**
- Modify: `apps/gui/ArchiveModel.h`
- Modify: `apps/gui/ArchiveModel.cpp`
- Modify: `apps/gui/MainWindow.cpp`
- Test: `apps/gui/tests/GuiPresentationTest.cpp`

**Interfaces:**
- Consumes: `ArchiveModel`, `ArchiveSession`
- Produces:
  - Row 0 `..` entry in `ArchiveModel` when browsing subfolders (`currentFolder != ""`).
  - Status bar encryption padlock indicator in `MainWindow::updateStatus()`.

- [ ] **Step 1: Write failing test in `GuiPresentationTest.cpp`**

```cpp
void parentFolderRowNavigation() {
    // Verify that navigating into folder displays row 0 as ".." with folder_up icon
    // Double clicking row 0 navigates back up
}
```

- [ ] **Step 2: Run CTest to verify it fails**

Run: `. .\scripts\env.ps1; ctest --test-dir build-msvc -R presentation --output-on-failure`
Expected: FAIL

- [ ] **Step 3: Implement `..` row and status bar lock**

1. In `ArchiveModel.cpp`:
   - In `folderRows()`, if `folder` is not empty, prepend a special row: `name = ".."`, `folder = true`, `type = "Folder"`, `size = 0`.
   - In `ArchiveFilter::lessThan()`, keep `..` row permanently at index 0 regardless of sort column or order.
2. In `MainWindow.cpp`:
   - In `table_->doubleClicked`, if clicked row is `..`, invoke `up()` instead of opening.
   - In `chooseModify()` and selection actions, ignore `..` row if included in selections.
   - In `MainWindow::updateStatus()`: add a small `lock.svg` icon on the left of `statusBar()` when archive has encrypted entries, with tooltip indicating encryption status.
   - Update `archiveSummary_` text to display: `"Total X folders and Y bytes in Z files"`.

- [ ] **Step 4: Run CTest to verify it passes**

Run: `. .\scripts\env.ps1; ctest --test-dir build-msvc -R presentation --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add apps/gui/ArchiveModel.h apps/gui/ArchiveModel.cpp apps/gui/MainWindow.cpp apps/gui/tests/GuiPresentationTest.cpp
git commit -m "feat(gui): add parent folder row navigation and status bar encryption indicator"
```

---

### Task 5: Refined Light & Dark Themes, End-to-End Presentation & 100K Performance Verification

**Files:**
- Modify: `apps/gui/Theme.cpp`
- Modify: `apps/gui/tests/GuiPresentationTest.cpp`
- Create: `docs/ACCEPTANCE_WINRAR_UI.md`

**Interfaces:**
- Consumes: Tasks 1-4
- Produces:
  - Comprehensive QSS styling for WinRAR toolbar, hover effects, address bar, table selection, rubberband, and status bar.
  - 100K-item benchmark verification proving zero regression in rendering speed or memory.

- [ ] **Step 1: Refine QSS and color palettes in `Theme.cpp`**

1. Modern WinRAR Light Theme:
   - Window: `#f4f5f7`, Table Base: `#ffffff`, Toolbar surface: `#ffffff`, Border: `#d8dde5`.
   - Toolbar button hover: `#e2e8f0` with smooth `border-radius: 8px`.
   - Active selection: `#2563eb` with crisp white text.
2. Modern WinRAR Dark Theme:
   - Window: `#13151a`, Table Base: `#191c22`, Toolbar surface: `#1f232b`, Border: `#2e3440`.
   - Toolbar button hover: `#282e3a` with smooth `border-radius: 8px`.
   - Active selection: `#1d4ed8` with crisp white text.
3. Ensure rubberband and table header sections align cleanly with WinRAR aesthetic.

- [ ] **Step 2: Add full UI verification and 100K benchmark in `GuiPresentationTest.cpp`**

1. Verify theme switching updates all toolbar buttons, rubberband style, and icon contrast.
2. Execute benchmark test verifying that 100,000 files load and scroll in < 150ms with zero frame drops during marquee selection.

- [ ] **Step 3: Run full verification suite**

Run:
- `. .\scripts\env.ps1; ctest --test-dir build-msvc --output-on-failure`
- `cargo test --workspace`
- `cargo clippy --workspace -- -D warnings`
- `cargo fmt --check`
Expected: ALL PASS (100%)

- [ ] **Step 4: Commit**

```bash
git add apps/gui/Theme.cpp apps/gui/tests/GuiPresentationTest.cpp docs/ACCEPTANCE_WINRAR_UI.md
git commit -m "feat(gui): finalize modern WinRAR light and dark themes with 100K performance verification"
```
