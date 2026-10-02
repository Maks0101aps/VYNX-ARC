# Format support

| Format | Browse / extract / test | Create | Passwords | Evidence |
|---|---|---|---|---|
| ZIP | Yes | Yes | AES-256 write/read; legacy read depends on backend | Real round trips, encrypted round trips, CRC failure, cancellation |
| 7Z | Yes | Yes | AES data and header encryption | Real round trips and wrong/correct password tests |
| RAR4 / RAR5 | Yes (Windows) | No | Data and encrypted filename decoding | Official UnRAR 7.23; stored, compressed, multi-file, RAR4/RAR5 encrypted fixtures |
| TAR | Yes | Yes | No | Unicode, empty files/directories and binary round trips |
| TAR.GZ / TGZ | Yes | Yes | No | Same round-trip corpus |

Link-containing archives are currently rejected for the whole operation, even if
the selected entries exclude links. Solid archives can require decoding preceding
unselected entries. TAR metadata browsing may need to scan/decode the full archive.
Source signatures, not filename extensions, identify manually opened archives.

Not yet supported: standalone GZIP/XZ/BZIP2/ZSTD/LZMA, TAR.XZ/TAR.BZ2/TAR.ZST,
CAB/ISO/WIM/CPIO/AR and 7Z split volumes. RAR decoder can locate subsequent parts;
full multivolume behavior and exact missing-volume messages still need validation.
ZIPX compatibility is codec-specific and is not advertised as a general capability.
