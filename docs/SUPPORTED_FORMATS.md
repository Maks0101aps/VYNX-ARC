# Format capability matrix

Current 0.1.0 development capabilities; 0.2.0 acceptance remains blocked.
Yes describes implemented behavior, not qualification of every possible codec.

| Format | Browse | Test | Extract | Create | Modify | Encrypted read | Encrypted write | Multi-volume read | Multi-volume create | Known limits |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| ZIP | Yes | Yes | Yes | Yes | Add/delete/rename | AES; legacy ZipCrypto through backend | AES-256 data | No | No | Backend codecs only; no general ZIPX claim; incomplete metadata preservation |
| 7Z | Yes | Yes | Yes | Yes | Add/delete/rename | AES data and headers | AES data and headers | See split 7Z | See split 7Z | Solid streams may decode unselected entries; incomplete metadata preservation |
| split 7Z | Yes | Yes | Yes | Yes | No | AES data and headers | AES before splitting | Yes | Yes | `.7z.001` numbering; equal-size parts, shorter final part; 4096-part read cap; minimum 64 KiB creation part; multi-file publication is not atomic |
| RAR4 | Yes (Windows) | Yes (Windows) | Yes (Windows) | No | No | Data and encrypted filenames | No | See RAR multipart | No | Read-only UnRAR 7.23; in-process decoder serialization; 512 MiB dictionary limit |
| RAR5 | Yes (Windows) | Yes (Windows) | Yes (Windows) | No | No | Data and encrypted filenames | No | See RAR multipart | No | Same read-only decoder and limits as RAR4 |
| RAR multipart | Yes (Windows) | Yes (Windows) | Yes (Windows) | No | No | Backend-supported encrypted RAR | No | Yes, RAR4/RAR5 | No | Start at first part; exact missing-volume path; encrypted multipart combinations not independently qualified |
| TAR | Yes | Yes | Yes | Yes | No | No | No | No | No | Stream scanning; regular files/directories only |
| TAR.GZ / TGZ | Yes | Yes | Yes | Yes | No | No | No | No | No | Stream decoding for metadata; regular files/directories only |

Browse reads metadata. Test decodes regular-file streams and enforces policy and
available checksums; it is not a content authenticity check. Link-containing
archives are rejected for the whole test/extraction/modification operation even
when selected entries exclude links. All formats use Windows-aware path, case
collision and output-limit checks. Unicode normalization collisions remain
unresolved. Passwords are supplied through the GUI/core; the CLI intentionally
has no password command-line option.

Signatures identify manually opened archives. Split-7Z filenames select the
numbered reader before signature detection. Split creation stages/verifies the
whole archive before publishing parts; see [VOLUMES.md](VOLUMES.md).

Unsupported: standalone GZIP/XZ/BZIP2/ZSTD/LZMA, TAR.XZ/TAR.BZ2/TAR.ZST,
CAB/ISO/WIM/CPIO/AR, split ZIP, and RAR creation/modification. ZIPX compatibility
is codec-specific and is not advertised broadly.
