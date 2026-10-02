# Dependency decisions (2026-10-02)

Versions are exact in Cargo manifests and Cargo.lock, including an isolated local
patch for the UnRAR decoder. Qt is pinned to 6.12.0. Rust is pinned to 1.99.0.
Developer SDK downloads came from Qt's official repository; package SHA-1 files
were checked before extraction. Qt package digests are integrity checks, not a
substitute for a publisher signature. End-user runtime DLLs are built/deployed
from the same SDK. No installed archiver is invoked.

| Dependency | Purpose / version / license | Build and update procedure | Security considerations |
|---|---|---|---|
| [Qt](https://doc.qt.io/qt-6/licensing.html) | Widgets, Core, Gui, Concurrent; 6.12.0; LGPLv3 selected | Dynamic MinGW x64 SDK; update SDK version together with compiler and CI, review notices, deploy DLLs with windeployqt | Retain LGPL/GPL texts, replacement/relink rights and source link. No browser engine. No Qt Network module is linked. |
| [CXX](https://github.com/dtolnay/cxx) | Typed Rust/C++ bridge; 1.0.202; MIT OR Apache-2.0 | Cargo cxx and cxx-build exact matching versions; regenerate bridge with Cargo | Result errors map to C++ exceptions; operation functions catch panics; opaque archive and operation handles |
| [zip](https://github.com/zip-rs/zip2) | ZIP metadata, stream decoding/writing, AES; 8.6.0; MIT | Cargo; change exact version and lockfile, rerun encrypted/corrupt/round-trip tests | Never call ZIP convenience extraction helpers. CRC and size checks run while streaming. ZIPX support depends on actual codec, not extension. |
| [sevenz-rust2](https://github.com/hasenbanck/sevenz-rust2) | Pure Rust 7Z reading/writing, AES and encrypted headers; 0.23.0; Apache-2.0 | Cargo; update exact version, inspect parser and dictionary bounds, run independent-tool comparisons | Solid decompression can decode skipped files. Default backend has its own allocations; complete allocation-budget audit still required. Password type internally copies UTF-16 without guaranteed zeroization. |
| [UnRAR](https://www.rarlab.com/rar_add.htm) | RAR4/RAR5 read-only decoder; 7.23; separate UnRAR license | Vendored official source via `unrarsrc-7.2.7.tar.gz`; source SHA-256 in third_party/unrar-sys/UPSTREAM.json. Bindings are unrar-ng-sys 0.7.7; source package is pinned locally. Review official release/security notes before upgrading. | 7.23 replaces outdated bundled beta source; includes upstream security fixes. No RAR encoder. Output uses RAR_TEST callback bytes, never native extraction paths. Links and large dictionaries blocked. Callback FFI is isolated with SAFETY comments and tests. |
| [tar](https://github.com/alexcrichton/tar-rs) | TAR streaming; 0.4.46; MIT OR Apache-2.0 | Cargo; update manifest/lock, test long names, sparse metadata and links | Convenience unpack helpers never used. Non-file/non-directory entries blocked. PAX/resource-budget review remains. |
| [flate2](https://github.com/rust-lang/flate2-rs) | TAR.GZ codec; 1.1.10; MIT OR Apache-2.0 | Cargo; update lock, round-trip/truncation tests | Streaming decode; gzip finalization errors must propagate. |
| [sha2](https://github.com/RustCrypto/hashes) | SHA-256; 0.11.0; MIT OR Apache-2.0 | Cargo exact version; known-vector tests | Hashes are local. Hashing does not execute archive contents. |
| [crc32fast](https://github.com/srijs/rust-crc32fast) | CRC32; 1.5.2; MIT OR Apache-2.0 | Cargo exact version; known-vector tests | CRC32 is not cryptographically secure. |
| [tempfile](https://github.com/Stebalien/tempfile) | Exclusive temporary creation and publication; 3.27.0; MIT OR Apache-2.0 | Cargo; test cancellation, conflicts and replace failures | Unique sibling temp files; default publication is no-clobber. RAII cleanup owns only files it created. |
| [zeroize](https://github.com/RustCrypto/utils) | Rust-owned secret buffers; 1.9.0; MIT OR Apache-2.0 | Cargo; review copies across each UI/FFI boundary | GUI strings and backend-created password copies are not yet comprehensively zeroized. Never persisted or logged. |

Transitive components include RustCrypto AES/digest utilities, lzma-rust2,
libbz2-rs-sys, zstd/zstd-sys and zlib-rs. Cargo.lock is authoritative for exact
versions. Release `licenses/CARGO_LICENSES.json` lists license IDs and sources;
the collection script copies upstream copyright/license texts including nested
native components. Re-run cargo audit after any dependency changes. A clean
audit only covers known RustSec advisories, not all native Qt/UnRAR vulnerabilities.

The upstream dependency repositories and recent official releases were reviewed
for maintenance and Windows compatibility. Local builds/tests verify this exact
Windows GNU configuration. ARM64 and MSVC remain unverified.

Build-only components: CMake 4.4.3 (BSD-3-Clause), Ninja 1.13.2 (Apache-2.0),
aqtinstall 3.3.0 (MIT), clang-format 23.1.2 (Apache-2.0 with LLVM exception),
Inno Setup 6.7.3 (its upstream license), cargo-audit 0.22.2 (MIT OR Apache-2.0).
They are not bundled as application runtime dependencies.
