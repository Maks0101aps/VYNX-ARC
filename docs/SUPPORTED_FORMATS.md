# Format capability matrix

Current 0.9.0 candidate capabilities. Qualification: [ACCEPTANCE_0_9.md](ACCEPTANCE_0_9.md).
Yes describes implemented behavior, not qualification of every possible codec.

| Format | Browse | Test | Extract | Create | Modify | Encrypted read | Encrypted write | Multi-volume read | Multi-volume create | Known limits |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| ZIP | Yes | Yes | Yes | Yes | Add/delete/rename/replace content | AES; legacy ZipCrypto through backend | AES-256 data | No | No | Backend codecs only; no general ZIPX claim; incomplete metadata preservation |
| 7Z | Yes | Yes | Yes | Yes | Add/delete/rename/replace content | AES data and headers | AES data and headers | See split 7Z | See split 7Z | Solid streams may decode unselected entries; incomplete metadata preservation |
| split 7Z | Yes | Yes | Yes | Yes | No | AES data and headers | AES before splitting | Yes | Yes | `.7z.001` numbering; equal-size parts, shorter final part; 4096-part read cap; minimum 64 KiB creation part; multi-file publication is not atomic |
| RAR4 | Yes (Windows) | Yes (Windows) | Yes (Windows) | No | No | Data and encrypted filenames | No | See RAR multipart | No | Read-only UnRAR 7.23; in-process decoder serialization; 512 MiB dictionary limit |
| RAR5 | Yes (Windows) | Yes (Windows) | Yes (Windows) | Yes, native rars | Add/delete/rename/replace (single, unlocked, unsigned) | Data and encrypted filenames | AES-256 data; optional encrypted headers | See RAR multipart | Yes | Levels 0/1/3/5; solid; dictionary subject to resource cap; recovery generation 1..10%; incomplete metadata preservation; no recovery repair |
| RAR multipart | Yes (Windows) | Yes (Windows) | Yes (Windows) | RAR5 only | No | Backend-supported encrypted RAR | RAR5 AES data and headers | Yes, RAR4/RAR5 | RAR5 only | First-part opening; .part1.rar naming; 64 KiB minimum payload plus framing overhead; 4096 parts; publication is not atomic |
| TAR | Yes | Yes | Yes | Yes | No | No | No | No | No | Stream scanning; regular files/directories only |
| TAR.GZ / TGZ | Yes | Yes | Yes | Yes | No | No | No | No | No | Stream decoding for metadata; regular files/directories only |
| TAR.XZ / TAR.BZ2 / TAR.ZST | Yes | Yes | Yes | Yes | No | No | No | No | No | Stream scanning; regular files/directories only |
| GZIP / XZ / BZIP2 / ZSTD / LZMA | Yes (one entry) | Yes | Yes | Yes (one regular file) | No | No | No | No | No | Name derived from container filename; metadata browsing decodes the stream; no archive-level encryption |

Browse reads metadata. Test decodes regular-file streams and enforces policy and
available checksums; it is not a content authenticity check. Link-containing
archives are rejected for the whole test/extraction/modification operation even
when selected entries exclude links. All formats use Windows-aware path, case
collision and output-limit checks. Unicode normalization collisions remain
unresolved. Passwords are supplied through the GUI/core or bounded CLI
`--password-stdin`; no password command-line value is accepted.

Signatures identify manually opened archives. Split-7Z filenames select the
numbered reader before signature detection. Split creation stages/verifies the
whole archive before publishing parts; see [VOLUMES.md](VOLUMES.md).

Unsupported: CAB/ISO/WIM/CPIO/AR, split ZIP, and RAR4 creation/modification. ZIPX compatibility
is codec-specific and is not advertised broadly.

Compressed TAR is identified by decoded USTAR metadata. Stream formats accept
exactly one regular input file; use a compressed TAR for folders or multiple
files. Resource modes constrain supported codecs and worker counts, not total
process memory in direct core/legacy CLI calls. Supervised GUI workers and native
RAR CLI create/modify additionally have
OS committed-memory caps of 768/1536/3072 MiB. Compression presets apply to new archive creation; ZIP/7Z
modification retains its existing rebuild policy.

Editable working copies and explicit save-back are available for supported ZIP/7Z/RAR5 entries up to 256 MiB. Split archives remain read-only. See [MODIFICATION.md](MODIFICATION.md).

Conversion accepts the supported readable source variants and writes a new ZIP or
7Z, including separately encrypted results. It verifies names, file hashes and
directory sets before publication, preserving the source. Representable modified
times are copied; comments, ACLs, ownership and extra metadata are not transferred.
Converting RAR contents does not add RAR encoding. The GUI working-copy dock also
offers Save as new archive through this same verified path.
