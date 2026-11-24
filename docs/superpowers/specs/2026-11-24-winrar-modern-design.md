# Design Document: Modern WinRAR-Inspired Interface & Vector Asset System

- **Date:** 2026-11-24
- **Status:** Proposed
- **Target:** VYNX ARC Desktop GUI (Qt 6, C++20, Windows 10/11)
- **Topic:** Modern WinRAR Layout, Rubberband Selection, Custom SVG Assets, Light & Dark Themes

---

## 1. Intent & The Why

VYNX ARC combines modern high-performance Rust core archiving (fail-closed, transactional, zero data loss) with a responsive C++20 Qt 6 GUI. Users love the familiar ergonomics and instant muscle memory of classic archivers like WinRAR—specifically its large toolbar buttons with text underneath, clear navigation bar with up-folder controls, and marquee (rubberband) mouse selection in the file list.

However, traditional 1990s/XP-era WinRAR interfaces suffer from outdated bitmapped icons, poor high-DPI scaling, lack of a refined dark theme, and clunky styling.

**The Goal:** Provide a sleek, modern "Neo-Classic WinRAR" design:
1. Instantly recognizable WinRAR-style layout: prominent top toolbar with large icons and labels underneath, path bar with Up button (`↑`), status bar with encryption lock indicator and byte counts.
2. Smooth rubberband / marquee cursor selection ("обводить курсором") in the file table for effortless multi-file selection.
3. Dedicated set of high-clarity, professional SVG vector assets for file types (`exe`, `folder`, `txt`, `archive`, `code`, `media`, etc.) and toolbar actions.
4. Cohesive, elegant Light and Dark themes that feel native, modern, and comfortable on Windows 10/11.

---

## 2. Goals, Non-Goals & Anti-Goals

### 2.1 Goals
- **WinRAR Ergonomic Layout:** Prominent top action toolbar with large icons and labels beneath (`Add`, `Extract To`, `Test`, `View`, `Delete`, `Find`, `Wizard`, `Info`, `Repair`), with logical cluster separators.
- **Address & Navigation Bar:** Navigation bar featuring an `↑` (Up) button, path dropdown/breadcrumb display, and responsive search bar.
- **Rubberband / Marquee Selection:** Clicking and dragging on the empty space or across rows in `QTableView` displays a semi-transparent selection rectangle that interactively selects intersected rows.
- **`..` Parent Directory Entry:** Inside subfolders of an archive, row 0 displays a `..` ("Up one level") entry with a distinct `folder_up` icon, double-clicking which navigates up.
- **Custom SVG Vector Asset Library:** Vector-based, pixel-aligned SVG icons in `assets/icons/` loaded via Qt Resource System (`.qrc`), scaling cleanly from 100% to 250% DPI without blur.
- **First-Class Dark & Light Themes:** Polished dark mode (`#13151a` / `#191c22` / `#20242c`) and clean light mode (`#f4f5f7` / `#ffffff` / `#edf0f4`) with refined typography (`Segoe UI Variable` / `Segoe UI`).

### 2.2 Non-Goals
- Changing the underlying Rust archiving core or FFI semantics.
- Re-implementing a full Windows Explorer shell replacement outside archive viewing.
- Cluttering the UI with unconfigurable advertising or nag-screens (VYNX ARC remains 100% telemetry-free, ad-free, open software).

### 2.3 Anti-Goals
- Skeuomorphic or bitmap-pixelated 90s graphics that look blurry on 4K/retina screens.
- Sluggish mouse tracking or stuttering during rubberband selection over 100,000+ archive items.
- Breaking existing shortcuts (`F2` rename, `Del` delete, `Alt+Left` back, `Alt+Up` up, `Ctrl+A` select all).

---

## 3. Approaches Considered & Trade-offs

### Approach 1: Dedicated SVG Resource Assets + Neo-Classic WinRAR Layout + QRubberBand (Selected)
- Dedicated SVGs in `assets/icons/` embedded in `vynx.qrc` and rendered cleanly via `QSvgRenderer` / `QIcon`.
- Top action toolbar with `Qt::ToolButtonTextUnderIcon`, group dividers, and smooth hover pill styling.
- Navigation bar with `↑` button, address bar, and search.
- Event filter on `QTableView::viewport()` managing an interactive `QRubberBand` for marquee selection.
- `..` parent folder entry in `ArchiveModel` when in subfolders.
- *Pros:* Clean architecture, beautiful visual fidelity at any screen scaling, faithful to user request while looking modern and fresh.
- *Trade-offs:* Requires adding SVG asset files to git and Qt resource bundle.

### Approach 2: Procedural C++ QPainter Vector Icons Only
- Keep drawing all icons in C++ code inside `IconProvider.cpp` without SVG files.
- *Pros:* Zero external files.
- *Cons:* Extremely hard to maintain and visually inferior; drawing rich file icons (e.g., dual-tone folders, folded documents, binary gears) purely via coordinate math in C++ leads to hundreds of lines of brittle drawing code.

**Decision:** Approach 1 is unanimously chosen.

---

## 4. Detailed Specification

### 4.1 Toolbar Layout & Action Clusters
The main command bar in `ArchivePage` is placed prominently above the path bar:
- **Buttons (Icon + Text Underneath, 32x32 icon, ~56px button height):**
  - **Cluster 1 (Core Archiving):**
    - `Add` (`tb_add.svg`): Add files/folders to archive (`Ctrl+Shift+A`).
    - `Extract To` (`tb_extract.svg`): Extract selected or all files with destination dialog.
    - `Test` (`tb_test.svg`): Test archive integrity.
    - `View` (`tb_view.svg`): Internal file preview / inspection (`F3`).
    - `Delete` (`tb_delete.svg`): Delete selected files (`Del`).
    - `Rename` (`tb_rename.svg`): Rename selected entry (`F2`).
  - *Divider*
  - **Cluster 2 (Inspection & Navigation):**
    - `Find` (`tb_find.svg`): Focus search / filter files (`Ctrl+F`).
    - `Wizard / Batch` (`tb_wizard.svg`): Batch extraction & assisted operations (`BatchExtractDialog`).
    - `Info` (`tb_info.svg`): Archive properties and details (`Alt+Enter`).
  - *Divider*
  - **Cluster 3 (Tools & Verification):**
    - `Verify / Repair` (`tb_repair.svg`): Hash verification & extraction recovery (`ExtractionRecoveryDialog` / checksum check).
- Responsive overflow: when window width is constrained (< 780px), secondary tools (Cluster 2 & 3) fold gracefully into a clean `More...` dropdown menu, keeping primary archiving buttons accessible.

### 4.2 Path & Navigation Bar
Below the toolbar:
- `↑` (Up button): 32x32 rounded button with `up.svg`, navigates to parent directory (`Alt+Up`).
- Address/Breadcrumb box: Shows current path inside archive (e.g. `C:\Users\User\Desktop` or `archive.zip\documents\`), with folder icon prefix and clickable segments.
- Search box: Styled line edit with magnifying glass placeholder and clear button.

### 4.3 Rubberband / Marquee Selection ("Обводка курсором")
- Implemented via a viewport event filter on `table->viewport()`.
- When user presses Left Mouse Button on empty area (or outside cell text) and drags:
  - An interactive `QRubberBand` widget is shown in `QRubberBand::Rectangle` mode.
  - As mouse moves, the rectangle geometry updates smoothly.
  - Intersected row items are identified via `table->rowAt()` and bounding rect intersection, updating `table->selectionModel()` with `QItemSelectionModel::Select | QItemSelectionModel::Rows`.
  - Supports standard modifier keys: `Ctrl` to toggle/accumulate selection, `Shift` for contiguous range selection.
  - Style: Semi-transparent fill (`rgba(37, 99, 235, 0.2)` in light theme, `rgba(59, 130, 246, 0.25)` in dark theme) with a 1px solid border.

### 4.4 `..` Parent Folder Row
- When navigating into a subfolder (`folder != ""`), row 0 of `ArchiveModel` displays:
  - Name: `..`
  - Type: `System Folder` / `Folder`
  - Size: empty
  - Icon: `folder_up.svg` (folder icon with a small up arrow).
  - Pinned above sorted items during column sorting.
  - Behavior: Double-clicking or pressing Enter on `..` navigates up to the parent directory.

### 4.5 SVG Asset Suite (`assets/icons/`)
All SVGs are crafted in a modern, clean, 24x24 / 32x32 grid with rounded corners and consistent stroke weights.
To ensure exceptional aesthetics in both Light and Dark themes, icons use a refined dual-tone palette (e.g. vibrant amber for folders, royal blue for archives, emerald for test/check, crimson for delete, purple for executables) with adaptive SVG stroke/fill handling in `IconProvider`:
1. **Toolbar SVGs:**
   - `tb_add.svg`: Stack of archive volumes bound with an accent strap.
   - `tb_extract.svg`: Open folder with blue extraction arrow.
   - `tb_test.svg`: Clipboard with a green verification checkmark.
   - `tb_view.svg`: Clean document eye / preview icon.
   - `tb_delete.svg`: Modern recycle bin.
   - `tb_rename.svg`: Document with edit cursor badge.
   - `tb_find.svg`: Precision magnifying glass.
   - `tb_wizard.svg`: Modern wand with sparkles.
   - `tb_info.svg`: Information circle badge (`i`).
   - `tb_repair.svg`: Medical kit / wrench recovery icon.
   - `up.svg`: Upward navigation arrow.
   - `lock.svg`: Mini security padlock for encrypted archives.
2. **File Type SVGs:**
   - `folder.svg`: Modern amber/blue dual-tone folder.
   - `folder_up.svg`: Folder with upward navigation arrow (`..`).
   - `exe.svg`: Binary application icon with subtle gear/badge.
   - `txt.svg`: Clean lined text document.
   - `archive.svg`: Multi-layer compressed archive file.
   - `image.svg`: Photo document with mountains/sun glyph.
   - `audio.svg`: Musical note document.
   - `video.svg`: Filmstrip / play document.
   - `code.svg`: Document with `< / >` code brackets.
   - `pdf.svg`: Document with signature PDF badge.
   - `doc.svg`: Office document icon.
   - `iso.svg`: Disc image icon.
   - `wim.svg`: Windows system image container icon.
   - `file.svg`: Clean generic fallback file.

### 4.6 Light and Dark Themes
- **Light Theme:**
  - Window background: `#f4f5f7`
  - Table / Surface: `#ffffff`
  - Toolbar background: subtle flat `#ffffff` with soft bottom border `#e2e8f0`
  - Text: `#1e293b` (slate-900), Muted: `#64748b` (slate-500)
  - Selection: `#2563eb` (primary blue) with white text, or soft blue row background `#dbeafe` with `#1e40af` text when unfocused.
  - Hover: `#f1f5f9`.
- **Dark Theme:**
  - Window background: `#13151a`
  - Table / Surface: `#191c22`
  - Toolbar background: `#1f232b` with border `#2d333f`
  - Text: `#f1f5f9` (slate-100), Muted: `#94a3b8` (slate-400)
  - Selection: `#1d4ed8` with crisp white text.
  - Hover: `#282e3a`.

### 4.7 Status Bar & Encryption Lock Indicator
- Status bar includes:
  - Left: `lock.svg` icon displayed when the archive contains encrypted entries/headers (with tooltip showing encryption status), otherwise hidden.
  - Right: `archiveSummary_` widget showing total files, folders, and byte counts in the classic format: `"Total X folders and Y bytes in Z files"`.

---

## 5. Security & Stability Considerations

- **Event Filter Safety:** Mouse tracking and rubberband calculations must clamp to viewport boundaries, preventing integer overflows or out-of-bounds selection indexes.
- **Large Archive Performance (100,000+ files):** Rubberband selection uses `QTableView::visualRect()` and binary index lookup rather than iterating over 100,000 rows on every mouse movement tick.
- **Resource Bundling:** All SVG files are compiled into the binary via Qt Resource Compiler (`rcc`), preventing missing file errors or path redirection vulnerabilities.
