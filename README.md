# VYNX ARC

An offline Windows archive manager built with C++20 / Qt 6 Widgets and a Rust
archive core connected through CXX. This repository contains a working **0.4.0
development milestone**. The supplied master brief remains the V1 contract;
this milestone does not satisfy the full contract.

Open, browse, search, test and extract ZIP, 7Z, RAR4/RAR5, TAR, TAR.GZ,
TAR.XZ, TAR.BZ2, TAR.ZST and standalone GZIP/XZ/BZIP2/ZSTD/LZMA streams.
Create all of these except RAR, including AES-encrypted ZIP and encrypted 7Z
filenames. Choose Store/Fast/Balanced/Maximum compression and
Eco/Balanced/Maximum resource modes. Operations run asynchronously with
progress and cancellation; additional archive requests enter a visible FIFO queue.
The browser uses Qt model/view, with folder navigation, sortable columns,
keyboard shortcuts and drag-to-open. English, Ukrainian and Russian UI,
system/light/dark appearance, local recent history and portable settings are
available. No external archiver, browser engine, telemetry or service is used.

Search covers names and paths throughout the archive. Double-click a file or
press F3 for read-only text (UTF-8/UTF-16 with BOM), raster image or Hex preview.
The viewer reads up to 16 MiB, displays text up to 4 MiB and the first 64 KiB
in Hex. Internal preview creates no plaintext file. Ctrl+Enter offers external
opening of supported document/media types through a verified read-only temporary
copy, with explicit confirmation. Executables, scripts and shortcuts are excluded.

File > Batch extraction (Ctrl+Shift+E) queues archives into separate destination
folders. Pause/resume, reorder or cancel pending jobs in View > Operation queue;
double-click a row for error details. New creation requests can also be queued.

Extraction validates Windows paths and collisions, blocks links/reparse points,
checks output limits, writes unique temporary files and publishes completed
streams. Existing files are preserved by default. These protections are tested,
but complete parser allocation budgets and filesystem race hardening still need
review before a production release.

![Archive browser](docs/screenshots/browser-dark-uk-1.25.png)

## Run

Locally generated artifacts are under `dist/` (ignored by Git):

- `VYNX-ARC-Portable-x64.zip`: extract the complete directory and run `VynxArc.exe`.
- `VYNX-ARC-Setup-x64.exe`: per-user installer; optional shortcuts and Open With.
- `SHA256SUMS.txt`: SHA-256 checksums of those artifacts.

The binaries are unsigned. The portable package passed tests with only Windows
directories on PATH. Installer compilation passed; clean-VM installation and
uninstallation have not been tested. Windows 11 x64 is the tested platform.
No development SDK is required to run the packages.

## Build

Install the pinned Rust MSVC toolchain, Qt 6.12.0 MSVC SDK, Visual Studio 2022
C++ Build Tools and Windows SDK, CMake and Ninja. See [BUILD.md](docs/BUILD.md).

```powershell
$env:VYNX_QT_ROOT = "$env:LOCALAPPDATA\VynxArcDev\QtMSVC"
scripts/build.ps1
scripts/build-release.ps1
```

Release packaging additionally requires Python for license collection and Inno
Setup 6 for the installer. `-SkipInstaller` generates only the portable package.

The bundled CLI supports `list`, `extract`, `create`, `test`, `add`, `delete`,
`rename`, `hash`, `verify`, `hash-entry` and `verify-entry`.
Run `vynxarc-cli --help`. Password operations currently use the GUI; secrets on
the command line are intentionally unsupported.

## Project status

[IMPLEMENTATION_STATUS.md](docs/IMPLEMENTATION_STATUS.md) records evidence,
limitations and remaining work. [SUPPORTED_FORMATS.md](docs/SUPPORTED_FORMATS.md)
defines actual format capabilities. [SECURITY_MODEL.md](docs/SECURITY_MODEL.md)
documents protections and incomplete security gates.

The [0.4 milestone report](docs/MILESTONE_0_4.md) records the current feature
increment and verification. The [previous product report](docs/PRODUCT_DEVELOPMENT.md)
records the preceding format/resource work. The earlier [improvement report](docs/IMPROVEMENTS_2026-10-16.md)
records the preceding parser, preview and dependency changes. The [October 3 acceptance report](docs/RELEASE_ACCEPTANCE.md)
retains historical failures. Stable-release acceptance remains incomplete at
version 0.4.0. Run `scripts/check.ps1` for mandatory local source gates;
release packaging enforces those same gates.

The application is MIT licensed. Qt and UnRAR retain their separate licenses;
see [THIRD_PARTY_NOTICES.md](docs/THIRD_PARTY_NOTICES.md) and the packaged `licenses/`
directory. RAR encoding is not implemented.
