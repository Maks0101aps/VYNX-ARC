# Developer build (Windows x64)

The first validated path is Rust stable `x86_64-pc-windows-gnu`, Qt **6.12.0**
MinGW x64, GCC **13.1.0**, CMake **4.4.3**, and Ninja **1.13.2**. This avoids
requiring administrator rights or installing Visual Studio. End users need none
of these tools. MSVC remains an unvalidated build option.

Install developer tools from their official sites. Rust uses rustup; Qt may use
its official installer. Put the Qt-compatible compiler under
`$env:VYNX_QT_ROOT/Tools/mingw1310_64` or adapt `scripts/env.ps1` for your SDK layout.
Set `VYNX_QT_ROOT` to the Qt SDK root (containing bin, include, lib, plugins).
CMake and Ninja must be on PATH. Python is needed for release license collection;
aqt is an optional installation helper. Neither is a runtime dependency.

```powershell
$env:VYNX_QT_ROOT = "$env:LOCALAPPDATA\VynxArcDev\Qt"
scripts/bootstrap.ps1
scripts/build.ps1
scripts/build-release.ps1
```

Cargo.lock pins transitive Rust dependencies. CMake builds the release static core
and typed bridge before linking the GUI. `ctest` exercises the real CXX boundary.
Avoid mixing compiler ABI/runtime variants: GCC Qt must use GCC C++ bridge objects.

No script silently installs packages, changes Defender, edits file defaults, or
modifies global PATH. Tool paths are scoped to the current build process.

For the independent packaged verification, install developer-only Python packages
`psutil` and `py7zr`, then run `python scripts/verify-portable.py`. It creates unique
scratch directories below `.dev`, checks the deployed binaries with SDK variables
removed, tests independent archive readers and refreshes the screenshots and
`docs/VERIFICATION.json`. It does not install the application.
