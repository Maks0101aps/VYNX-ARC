# Third-party notices

VYNX ARC's MIT license applies only to application-owned code. Dependencies
retain their upstream licenses. Full license texts must accompany distributed
binaries in `licenses/` and `licenses/CARGO_LICENSES.json` identifies the pinned
Rust dependency graph.

| Component | Version | License | Upstream |
|---|---|---|---|
| Qt Core / Gui / Widgets / Concurrent | 6.12.0 | LGPL-3.0-or-later (selected open-source terms) | https://www.qt.io/ |
| MinGW GCC runtime | 13.1.0 | GPL-3.0 with GCC Runtime Library Exception; MinGW notices | https://gcc.gnu.org/ |
| CXX / cxx-build | 1.0.202 | MIT OR Apache-2.0 | https://github.com/dtolnay/cxx |
| zip | 8.6.0 | MIT | https://github.com/zip-rs/zip2 |
| sevenz-rust2 | 0.23.0 | Apache-2.0 | https://github.com/hasenbanck/sevenz-rust2 |
| sha2 | 0.11.0 | MIT OR Apache-2.0 | https://github.com/RustCrypto/hashes |
| crc32fast | 1.5.2 | MIT OR Apache-2.0 | https://github.com/srijs/rust-crc32fast |
| tempfile | 3.27.0 | MIT OR Apache-2.0 | https://github.com/Stebalien/tempfile |
| zeroize | 1.9.0 | MIT OR Apache-2.0 | https://github.com/RustCrypto/utils |
| tar | 0.4.46 | MIT OR Apache-2.0 | https://github.com/alexcrichton/tar-rs |
| flate2 | 1.1.10 | MIT OR Apache-2.0 | https://github.com/rust-lang/flate2-rs |

Qt is dynamically linked. Users may replace its compatible DLLs and build the
application from published source. This application does not prohibit reverse
engineering for debugging modifications to LGPL-covered libraries. Corresponding
Qt source: https://download.qt.io/official_releases/qt/6.12/6.12.0/submodules/
No Qt code has been modified. Qt commercial licensing is not required when the
applicable open-source obligations are met.

RAR decoding uses UnRAR **7.23**, copyright Alexander Roshal, under the separate
UnRAR license in `licenses/rust/unrar-ng-sys-0.7.7/vendor/unrar/license.txt`.
Bindings originate from unrar-ng-sys 0.7.7 (MIT OR Apache-2.0); the bundled older
decoder was replaced with official stable source. `third_party/unrar-sys/UPSTREAM.json`
records the source URL and checksum. No proprietary RAR encoder or WinRAR code is used.

UnRAR source code may be used in any software to handle RAR archives without
limitations free of charge, but cannot be used to develop RAR (WinRAR) compatible
archiver and to re-create RAR compression algorithm, which is proprietary.
Distribution of modified UnRAR source code in separate form or as a part of other
software is permitted, provided that full text of this paragraph, starting from
"UnRAR source code" words, is included in license, or in documentation if license
is not available, and in source code comments of resulting package.

Developer Python tooling is not a runtime dependency and is not shipped.

MSVC packages contain app-local Microsoft Visual C++ 2022 (VC143) runtime DLLs,
copyright Microsoft Corporation, distributed under Microsoft's Visual Studio
redistributable terms. They retain their separate license and are not MIT/Qt
components. The installed SDK's redistributable list is included in
`licenses/MSVC-Redist.txt`. Upstream list: https://aka.ms/vs/17/redist.txt.
