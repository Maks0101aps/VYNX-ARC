# VYNX ARC Post-1.0.0 Roadmap Specification

**Target Platform:** Windows 10/11 x64  
**Core Technologies:** Rust (2024 edition, MSVC), C++20, Qt 6 Widgets, CXX FFI, Windows APIs  
**Engineering Philosophy:** Zero telemetry, fail-closed security, asynchronous non-blocking UI, bounded memory/resource usage, deterministic testing.

---

## 1. Release Milestone Map

| Version | Codename | Primary Feature Focus | Key Capabilities |
|---|---|---|---|
| **v1.0.2** | *Optical Vanguard* | ISO 9660 & UDF Optical Disc Image Support | Pure Rust zero-allocation sector parser, Joliet UCS-2BE Unicode directory trees, Level 1/2/3 fallback, instant random-access preview/extraction, CRC32 verification, Windows Explorer & Inno Setup `.iso` association. |
| **v1.0.5** | *Windows Image Studio* | WIM & ESD Windows Imaging Format Support | Windows native `wimgapi.dll` dynamic bindings with fallback structured header reader, multi-image index browsing (Install.wim / Boot.wim / WinRE.wim), metadata inspection (Build, Architecture, Edition, Languages), LZX/XPRESS extraction, split `.swm` span reader. |
| **v1.0.9** | *Atomic Split Matrix* | Multi-Volume Archive Modification & Staged Rebuild | Safe modification of split archives (`.7z.001`..`.7z.NNN` and `.part1.rar`..`.partN.rar`), atomic volume staging in isolated temp directories, volume quota calculation, fail-safe rollback preserving original sets, recovery record recalculation. |

---

## 2. Milestone Details

### 2.1. Version 1.0.2: ISO 9660 & UDF Optical Disc Image Support

#### 2.1.1. Motivation & User Value
Users frequently handle ISO images for OS distribution, virtualization, backup snapshots, and archival installations. Currently, users are forced to mount ISOs as virtual drive letters or use heavy, bloated tools with telemetry. VYNX ARC v1.0.2 enables immediate, lightweight browsing, previewing, and extraction of ISO disc images as first-class read-only archives with zero virtual drive clutter.

#### 2.1.2. Architecture & Design
- **Sector Engine:** Standard optical discs use 2048-byte (2 KiB) sectors.
- **Volume Descriptor Parsing:**
  - System Area: Sectors 0..15 (32 KiB) reserved.
  - Volume Descriptors start at sector 16 (byte offset 32768, `0x8000`).
  - Read Primary Volume Descriptor (PVD, Type 1) and Supplementary Volume Descriptor (SVD, Type 2, Joliet).
  - Identifier check: Standard identifier `CD001` at offset 1..6.
  - UDF VRS recognition: `BEA01`, `NSR02`, `NSR03`, `TEA01`.
- **Unicode Support (Joliet):**
  - Joliet SVD provides UCS-2BE (Big-Endian UTF-16) filenames up to 64 or 128 characters.
  - PVD provides standard Level 1/2/3 uppercase 8.3/31-character ASCII filenames (with `;1` version string stripped).
- **Directory Traversal:**
  - Directory records contain extent LBA sector and byte length.
  - Enforce max entry limit (`Policy::default().max_entries`), maximum path depth (64 levels), and directory loop prevention.
- **Streaming & Extraction:**
  - Disc images are uncompressed; extracting a file is an offset seek (`LBA * 2048`) and direct sector stream copy (`Read::take`), calculating CRC32 on-the-fly.
  - Instant preview in GUI without temporary file creation.

---

### 2.2. Version 1.0.5: WIM & ESD Windows Imaging Format Support

#### 2.2.1. Motivation & User Value
System administrators, power users, and Windows enthusiasts frequently inspect `install.wim`, `boot.wim`, and compressed `install.esd` images (used in Windows 10/11 setup media). Standard archivers either fail on ESD (LZMS solid compression) or offer clunky interfaces.

#### 2.2.2. Architecture & Design
- **Engine Options:**
  - Windows Native: Dynamically load `wimgapi.dll` via `LoadLibraryW`/`GetProcAddress` on Windows.
  - Zero External Dependencies: Safe wrapper around `WIMCreateFile`, `WIMLoadImage`, `WIMExtractImage`, `WIMGetImageInformation`.
- **Multi-Image Architecture:**
  - WIM files contain multiple indexed images (e.g., Image 1: Windows 11 Home, Image 2: Windows 11 Pro).
  - Add image selector UI / root directory virtual nodes (`[1] Windows 11 Home/`, `[2] Windows 11 Pro/`).
- **Compression Support:**
  - Uncompressed, XPRESS, LZX, and LZMS (for ESD).
- **Security:**
  - Enforce path traversal protections against malicious WIM XML metadata or directory entries.

---

### 2.3. Version 1.0.9: Multi-Volume Archive Modification & Staged Rebuild

#### 2.3.1. Motivation & User Value
Multi-volume archives (`.7z.001`, `.part1.rar`) are historically strictly read-only in all modern archivers because updating a split archive requires rebuilding the entire multi-part chain, recalculating volume boundaries, and re-splitting. If interrupted, the entire archive set is destroyed.

#### 2.3.2. Architecture & Design
- **Transactional Staging:**
  - Never modify original volumes in-place.
  - Stage the new modified archive as a complete split sequence in a private workspace (`.vynx-staging-<uuid>/`).
  - Preflight disk space ensuring the target volume has at least 1.1x required capacity for both original and new volume chains.
- **Atomic Two-Phase Commit:**
  - Phase 1: Rebuild new split set with updated files, compression dictionary, and recovery records.
  - Phase 2: Atomic rename swap (`MoveFileExW` with `MOVEFILE_REPLACE_EXISTING`) of all parts.
  - Rollback on failure: delete staging files, leaving original multi-part set 100% intact.
- **User Experience:**
  - Clear warning and confirmation dialog estimating rebuild time and required disk space before starting.
