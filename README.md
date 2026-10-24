# VYNX ARC

An offline Windows archive manager built with C++20 / Qt 6 Widgets and a Rust
archive core connected through CXX. This repository contains the **0.9.0
development candidate**. The supplied master brief remains the V1 contract; this
milestone does not satisfy the full contract.

Open, browse, search, test and extract ZIP, 7Z, RAR4/RAR5, TAR, TAR.GZ,
TAR.XZ, TAR.BZ2, TAR.ZST and standalone GZIP/XZ/BZIP2/ZSTD/LZMA streams.
Create these formats, including native RAR5, AES-encrypted ZIP and encrypted 7Z
filenames. RAR4 remains read-only. RAR5 adds solid compression, AES/header
encryption, dictionary controls, recovery records and actual RAR5 volumes.
See [RAR capabilities and limits](docs/NATIVE_RAR.md) and the
[0.9 acceptance report](docs/ACCEPTANCE_0_9.md).
Choose Store/Fast/Balanced/Maximum compression and
Eco/Balanced/Maximum resource modes. Operations run asynchronously with
progress and cancellation; additional archive requests enter a visible FIFO queue.
Each archive has its own tab with independent search, navigation and selection.
Idle tabs remain available during another archive's operation. The browser uses Qt model/view, with folder navigation, sortable columns,
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
The bounded journal restores interrupted jobs for explicit review/retry; passwords
are never saved. Export a JSON report with terminal results and batch identifiers.

File > Convert archives (Ctrl+Shift+C) plans and queues supported source formats
into new ZIP/7Z archives, with independent source/destination encryption choices.
Originals are kept; output contents are decoded and verified before publication.
Retry failed batch jobs creates explicit new attempts with reviewed settings.
Large queue selections are stored completely in verified bounded sidecar files.
Unknown archive-write results require verification before retrying.

Archive decoders and writers used by the GUI run in supervised worker processes,
with Windows memory/process limits, a wall-time budget and forced cancellation of
unresponsive children. Other idle tabs and retained working copies survive a worker
failure. This is resource isolation, not a separate filesystem-permission sandbox.

Ctrl+Shift+Return creates an editable working copy of a supported ZIP/7Z/RAR5 file.
Use the View > Working copies dock to explicitly save it back, save as a new
archive, verify its contents, reload its source, export or discard it. Changed
sources stop a save; failed saves retain the edited copy.
Decrypted working content stays on disk until discarded. View > Temporary files
manages disposable read-only views separately. Tabs with pending edits show `*`.

Extraction validates Windows paths and collisions, blocks links/reparse points,
checks output limits, writes unique temporary files and publishes completed
streams. Existing files are preserved by default. These protections are tested,
but complete parser allocation budgets and filesystem race hardening still need
review before a production release.

![Working copies](docs/screenshots/0.6/working-copies-dark-uk-1.25.png)

## Run

Locally generated artifacts are under `dist/` (ignored by Git):

- `VYNX-ARC-Portable-x64.zip`: extract the complete directory and run `VynxArc.exe`.
- `VYNX-ARC-Setup-x64.exe`: per-user installer; optional shortcuts and Open With.
- `SHA256SUMS.txt`: SHA-256 checksums of those artifacts.

The binaries are unsigned. Package verification, the per-user upgrade path,
Open With registration and uninstallation are tested on the Windows development
host with an isolated runtime PATH. Independent clean-machine qualification and
Narrator review remain separate manual gates. Windows 11 x64 is the tested
platform. No development SDK is required to run the packages.

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
`rename`, `convert`, `hash`, `verify`, `hash-entry`, `verify-entry` and supervised
`isolated-list`, `isolated-test`, `isolated-extract`.
Run `vynxarc-cli --help`. Password operations currently use the GUI; secrets on
the command line are intentionally unsupported.

## Project status

The [0.8.0 acceptance report](docs/MILESTONE_0_8.md),
[performance report](docs/BENCHMARKS_0_8.md) and [plan](docs/ROADMAP_0_8.md)
record completed workflows, exact package identities and open gates. The
[0.7.0 acceptance report](docs/MILESTONE_0_7.md) records the accepted baseline. The
[0.6.0 acceptance report](docs/MILESTONE_0_6.md) and earlier
[roadmap](docs/ROADMAP_0_6.md) retain the preceding implementation history.

[IMPLEMENTATION_STATUS.md](docs/IMPLEMENTATION_STATUS.md) records evidence,
limitations and remaining work. [SUPPORTED_FORMATS.md](docs/SUPPORTED_FORMATS.md)
defines actual format capabilities. [SECURITY_MODEL.md](docs/SECURITY_MODEL.md)
documents protections and incomplete security gates.

The [0.5 milestone report](docs/MILESTONE_0_5.md) records the preceding behavior,
measurements and verification boundaries. The [0.4 milestone report](docs/MILESTONE_0_4.md)
records the historical feature increment. Stable-release acceptance remains
incomplete beyond the measured milestone scope. Run `scripts/check.ps1` for
mandatory local source gates; release packaging enforces those same gates.

The application is MIT licensed. Qt and UnRAR retain their separate licenses;
see [THIRD_PARTY_NOTICES.md](docs/THIRD_PARTY_NOTICES.md) and the packaged `licenses/`
directory. RAR encoding is not implemented.
