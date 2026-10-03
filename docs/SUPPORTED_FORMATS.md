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
| TAR.XZ | Yes | Yes | Yes | Yes | No | No | No | No | No | XZ/LZMA2; bounded stream decoding; regular files/directories only |
| TAR.BZ2 | Yes | Yes | Yes | Yes | No | No | No | No | No | BZIP2; bounded stream decoding; regular files/directories only |
| TAR.ZST | Yes | Yes | Yes | Yes | No | No | No | No | No | ZSTD; bounded decoder window; regular files/directories only |
| GZIP | Yes | Yes | Yes | Yes | No | No | No | No | No | One decoded file; creation requires one regular input file; Deflate levels 0/1/6/9 |
| XZ | Yes | Yes | Yes | Yes | No | No | No | No | No | One decoded file; LZMA2 presets and bounded dictionary; no Store mode |
| BZIP2 | Yes | Yes | Yes | Yes | No | No | No | No | No | One decoded file; compression levels 1/6/9 (Eco caps at 3); no Store mode |
| ZSTD | Yes | Yes | Yes | Yes | No | No | No | No | No | One decoded file; levels 1/3/19, bounded window; no Store mode |
| LZMA | Yes | Yes | Yes | Yes | No | No | No | No | No | Legacy .lzma header, one decoded file, single encoder thread, no native checksum or Store mode |

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

The added formats are development features on `feature/0.3-development`.
Standalone metadata requires a complete bounded decoding pass to discover size
and CRC32. Its one output name derives from the archive filename, not an embedded
untrusted filename. GZIP/XZ/BZIP2/ZSTD payloads with TAR headers are recognized
as compressed TAR; legacy `.lzma` remains a standalone stream. Embedded compressed
checksums are enforced where present. LZMA testing checks successful decoding and
the metadata-pass CRC, not an embedded checksum or authenticity guarantee.
TAR readers drain the compressed stream after the TAR end marker so trailer
errors are not hidden. See [DEVELOPMENT.md](DEVELOPMENT.md) for resource and preset
limits. Codec-library support is not blanket qualification of malformed inputs,
concatenation variants or every external producer.

Unsupported: CAB/ISO/WIM/CPIO/AR, split ZIP, and RAR creation/modification. ZIPX compatibility
is codec-specific and is not advertised broadly.
