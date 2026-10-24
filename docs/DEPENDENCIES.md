# Dependency decisions

0.9 adds exact rars 0.12.0 (Apache-2.0) for native RAR5 encoding; UnRAR
remains decoding-only. See [NATIVE_RAR.md](NATIVE_RAR.md) for API and limits.
ZIP 9.0.4 has a local MIT-licensed patch binding construction to the preflighted
directory without end-record fallback. Provenance and upgrade instructions:
[third_party/zip/PATCHES.md](../third_party/zip/PATCHES.md). Full
COPYING/LICENSE/NOTICE texts and resolved metadata are collected at packaging.

Earlier dependency decisions (2026-10-16):

ZIP moved from yanked 8.6.0 to stable 9.0.4. The maintainer explicitly attributes
the old-version yanks to security issues in
[issue 1075](https://github.com/zip-rs/zip2/issues/1075#issuecomment-6209381974),
including GHSA-vv5m-jxxh-4627, GHSA-4v9h-p7m9-rh26 and GHSA-4684-h2q9-2vcp.
This is not a claim that every upstream issue was exploitable through VYNX's
custom extraction paths. ZIP names now use the fallible 9.x decoding API.
The lockfile also updates ZIP's transitive codec dependencies.

7Z is a local source patch of 0.23.0 because the upstream reader does not expose
the required application metadata/dictionary budget configuration. Provenance,
limits and upgrade instructions are in
[third_party/sevenz-rust2/PATCHES.md](../third_party/sevenz-rust2/PATCHES.md).

Versions are exact in Cargo manifests and Cargo.lock, including an isolated local
patch for the UnRAR decoder. Qt is pinned to 6.12.0. Rust is pinned to 1.99.0.
Developer SDK downloads came from Qt's official repository; package SHA-1 files
were checked before extraction. Qt package digests are integrity checks, not a
substitute for a publisher signature. End-user runtime DLLs are built/deployed
from the same SDK. No installed archiver is invoked.

| Dependency | Purpose / version / license | Build and update procedure | Security considerations |
|---|---|---|---|
| [Qt](https://doc.qt.io/qt-6/licensing.html) | Widgets, Core, Gui, Concurrent; 6.12.0; LGPLv3 selected | Dynamic MSVC 2022 x64 SDK; update SDK version together with compiler and CI, review notices, deploy DLLs with windeployqt | Retain LGPL/GPL texts, replacement/relink rights and source link. No browser engine. No Qt Network module is linked. |
| [CXX](https://github.com/dtolnay/cxx) | Typed Rust/C++ bridge; 1.0.202; MIT OR Apache-2.0 | Cargo cxx and cxx-build exact matching versions; regenerate bridge with Cargo | Result errors map to C++ exceptions; operation functions catch panics; opaque archive and operation handles |
| [zip](https://github.com/zip-rs/zip2) | ZIP metadata, stream decoding/writing, AES; 9.0.4; MIT | Cargo; change exact version and lockfile, rerun encrypted/corrupt/round-trip tests | Bounded central-directory preflight before backend parsing. Never call convenience extraction helpers. Codec/output limits are separate concerns. |
| [sevenz-rust2](https://github.com/hasenbanck/sevenz-rust2) | Pure Rust 7Z reading/writing, AES and encrypted headers; patched 0.23.0; Apache-2.0 | Rebase the documented local budget patch on a verified upstream crate; rerun parser regressions and independent readers | Header/metadata/decoder-chain budgets are enforced before affected allocations. These are not a whole-process RSS cap. Backend password copies still need review. |
| [UnRAR](https://www.rarlab.com/rar_add.htm) | RAR4/RAR5 read-only decoder; 7.23; separate UnRAR license | Vendored official source via `unrarsrc-7.2.7.tar.gz`; source SHA-256 in third_party/unrar-sys/UPSTREAM.json. Bindings are unrar-ng-sys 0.7.7; source package is pinned locally. Review official release/security notes before upgrading. | 7.23 replaces outdated bundled beta source; includes upstream security fixes. No RAR encoder. Output uses RAR_TEST callback bytes, never native extraction paths. Links and large dictionaries blocked. Callback FFI is isolated with SAFETY comments and tests. |
| [tar](https://github.com/alexcrichton/tar-rs) | TAR streaming; 0.4.46; MIT OR Apache-2.0 | Cargo; update manifest/lock, test long names, sparse metadata and links | Convenience unpack helpers never used. Non-file/non-directory entries blocked. PAX/resource-budget review remains. |
| [flate2](https://github.com/rust-lang/flate2-rs) | TAR.GZ codec; 1.1.10; MIT OR Apache-2.0 | Cargo; update lock, round-trip/truncation tests | Streaming decode; gzip finalization errors must propagate. |
| [sha2](https://github.com/RustCrypto/hashes) | SHA-256; 0.11.0; MIT OR Apache-2.0 | Cargo exact version; known-vector tests | Hashes are local. Hashing does not execute archive contents. |
| [crc32fast](https://github.com/srijs/rust-crc32fast) | CRC32; 1.5.2; MIT OR Apache-2.0 | Cargo exact version; known-vector tests | CRC32 is not cryptographically secure. |
| [tempfile](https://github.com/Stebalien/tempfile) | Exclusive temporary creation and publication; 3.27.0; MIT OR Apache-2.0 | Cargo; test cancellation, conflicts and replace failures | Unique sibling temp files; default publication is no-clobber. RAII cleanup owns only files it created. |
| [zeroize](https://github.com/RustCrypto/utils) | Rust-owned secret buffers; 1.9.0; MIT OR Apache-2.0 | Cargo; review copies across each UI/FFI boundary | GUI strings and backend-created password copies are not yet comprehensively zeroized. Never persisted or logged. |

Direct codec dependencies now include bzip2 0.6.1 (MIT OR Apache-2.0),
zstd 0.14.2 with zstdmt (MIT), and lzma-rust2 0.22.0 (Apache-2.0). They
provide standalone streams and compressed TAR. Codec upgrades require independent
Python decoder/encoder interoperability and truncation/cancellation tests.
Resource profiles bound codec settings; they are not process memory quotas.

Transitive components include RustCrypto AES/digest utilities,
libbz2-rs-sys, zstd-sys and zlib-rs. Cargo.lock is authoritative for exact
versions. Release `licenses/CARGO_LICENSES.json` lists license IDs and sources;
the collection script copies upstream copyright/license texts including nested
native components. Re-run cargo audit after any dependency changes. A clean
audit only covers known RustSec advisories, not all native Qt/UnRAR vulnerabilities.

The upstream dependency repositories and recent official releases were reviewed
for maintenance and Windows compatibility. Local builds/tests verify this exact
Windows MSVC x64 configuration. Earlier GNU evidence is historical; ARM64 remains
unverified. See TOOLCHAIN.md for the migration and old GNU linker warning.

Build-only components: CMake 4.4.3 (BSD-3-Clause), Ninja 1.13.2 (Apache-2.0),
aqtinstall 3.3.0 (MIT), clang-format 23.1.2 (Apache-2.0 with LLVM exception),
Inno Setup 6.7.3 (its upstream license), cargo-audit 0.22.2 (MIT OR Apache-2.0).
They are not bundled as application runtime dependencies.
