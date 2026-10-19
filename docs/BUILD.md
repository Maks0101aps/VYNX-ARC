# Developer build (Windows x64)

The current build uses Rust **1.99.0** `x86_64-pc-windows-msvc`, Qt **6.12.0**
MSVC 2022 x64, Visual Studio Build Tools **17.14.41** / MSVC **14.44.35207**,
Windows SDK **10.0.26100.0**, CMake **4.4.3**, and Ninja **1.13.2**.
End users need none of these tools. The former GNU milestone is preserved in
the initial Git checkpoint. Never mix its CXX objects with MSVC Qt libraries.

Install developer tools from their official sites. Rust uses rustup; Qt may use
its official installer. Install Visual Studio Build Tools with the C++ build
workload and Windows SDK. `scripts/env.ps1` uses vswhere / Developer PowerShell
to configure the compiler for this process without changing global PATH.
Set `VYNX_QT_ROOT` to the Qt SDK root (containing bin, include, lib, plugins).
CMake and Ninja must be on PATH. Python is needed for release license collection;
aqt is an optional installation helper. Neither is a runtime dependency.

Qt 6.11+ Windows repositories are split by compiler architecture. Released aqt
3.3.0 does not resolve the Qt 6.12 MSVC repository correctly. CI pins upstream
aqt commit `076e1659807d0b362a3ed684d54c2e9c775eb9c7` through install-qt-action's
`aqtsource` input; ordinary Qt archive checksum verification remains enabled.
This fixes the observed SDK installation failure, without changing the Qt version.

```powershell
$env:VYNX_QT_ROOT = "$env:LOCALAPPDATA\VynxArcDev\QtMSVC"
scripts/bootstrap.ps1
scripts/build.ps1
scripts/build-release.ps1
```

An additional developer-only C++ AddressSanitizer build was exercised with:

```powershell
. scripts/env.ps1
cmake -S . -B .dev/build-asan-0.6 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl "-DCMAKE_PREFIX_PATH=$env:VYNX_QT_ROOT" "-DCMAKE_CXX_FLAGS=/fsanitize=address /Zi /D_DISABLE_VECTOR_ANNOTATION /D_DISABLE_STRING_ANNOTATION" -DCMAKE_EXE_LINKER_FLAGS=/INCREMENTAL:NO
cmake --build .dev/build-asan-0.6 --parallel 6
ctest --test-dir .dev/build-asan-0.6 --output-on-failure
```

STL annotations are disabled consistently because the Rust bridge/native static
libraries are not instrumented. GUI/shell C++ accesses retain ASan instrumentation;
this is not a Rust/UnRAR/Qt sanitizer build or leak detector. The ASan runtime must
be discoverable from the developer environment. Never package this binary as the
normal release. The release target and its Rust library remain uninstrumented.

Cargo.lock pins transitive Rust dependencies. CMake builds the release static core
and typed bridge before linking the GUI. `ctest` exercises the real CXX boundary.
The build directory is `build-msvc`; MSVC runtime DLLs are deployed app-locally.
No MinGW runtime DLLs are required by the MSVC package.

`scripts/check.ps1` is the mandatory local quality entry point: fmt, Clippy with
warnings denied, locked workspace tests, cargo audit, native CMake build and CTest.
Install the pinned `cargo-audit` 0.22.2 beforehand; the check command does not
install or update tools. CI installs that exact version with `--locked`.
`build.ps1` delegates to this command, so `build-release.ps1` stops before staging
or packaging when a mandatory source gate fails.

No script silently installs packages, changes Defender, edits file defaults, or
modifies global PATH. Tool paths are scoped to the current build process.

For the independent packaged verification, install developer-only Python packages
`psutil` and `py7zr`, then run `python scripts/verify-portable.py`. It creates unique
scratch directories below `.dev`, checks the deployed binaries with SDK variables
removed, tests independent archive readers and refreshes the screenshots and
`docs/VERIFICATION.json`. It does not install the application.
