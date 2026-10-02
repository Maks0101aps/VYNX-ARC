# Windows toolchain investigation

The 0.1.0 Qt MinGW compiler's `ld --version` reports GNU Binutils **2.39**.
Rust 1.99.0 ships a separate self-contained GNU ld **2.44**. The build selected
the older Qt compiler linker, which cannot parse LLVM's newer `.drectve`
`-exclude-symbols` directive. This is the documented upstream incompatibility:
https://github.com/rust-lang/rust/issues/112368

The warning was not suppressed. The supported build moves the entire C++ ABI
boundary to MSVC: Rust Windows MSVC, CXX-generated C++ compiled by cl.exe,
Qt's corresponding MSVC SDK and the real Microsoft linker/Windows SDK.
Qt requires consistent compiler ABI:
https://doc.qt.io/qt-6/qt-releases.html#binary-compatibility

The native app manifest is explicitly supplied as a target source, preventing
Qt 6.12 from generating a conflicting default manifest. CXX remains intact.
The release script deploys the app-local VC143 CRT. It does not require MinGW
or a developer installation on the end-user computer.

The local user Cargo configuration contains unrelated experimental SDK/linker
settings and an unused vswhom patch. Build scripts select Microsoft's real
link.exe explicitly and clear inherited encoded Rust flags in their process.
They do not edit the user's global Cargo configuration. An unused-patch warning
from that external configuration can remain; it is unrelated to `.drectve`.
