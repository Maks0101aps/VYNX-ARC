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

```powershell
$env:VYNX_QT_ROOT = "$env:LOCALAPPDATA\VynxArcDev\QtMSVC"
scripts/bootstrap.ps1
scripts/build.ps1
scripts/build-release.ps1
```

Cargo.lock pins transitive Rust dependencies. CMake builds the release static core
and typed bridge before linking the GUI. `ctest` exercises the real CXX boundary.
The build directory is `build-msvc`; MSVC runtime DLLs are deployed app-locally.
No MinGW runtime DLLs are required by the MSVC package.

No script silently installs packages, changes Defender, edits file defaults, or
modifies global PATH. Tool paths are scoped to the current build process.

For the independent packaged verification, install developer-only Python packages
`psutil` and `py7zr`, then run `python scripts/verify-portable.py`. It creates unique
scratch directories below `.dev`, checks the deployed binaries with SDK variables
removed, tests independent archive readers and refreshes the screenshots and
`docs/VERIFICATION.json`. It does not install the application.
