# Split volumes

Open numbered `.7z.001` volumes for browsing, testing, hashing and extraction.
Opening another numbered part selects `.001`. The reader keeps one data handle
open at a time and indexes at most 4096 parts. It supports standard equal-size
parts with a shorter final part; the first must contain the 32-byte 7Z signature
header. Required length is derived from the checked next-header offset/size.
Missing parts report `MISSING_VOLUME` with the exact path. These archives are
read only for add/delete/rename.

The creation dialog offers optional 7Z splitting at 100/500 MiB, 1/4 GiB, or a
custom MiB size. CLI: `vynxarc-cli create backup.7z INPUT --split-bytes 104857600`.
The output is `backup.7z.001`, `.002`, etc. Core sizes must be at least 64 KiB;
creation refuses existing outputs and colliding part names without replacing them.
Encryption applies before splitting, including encrypted headers.

A complete archive is staged and decoded for verification before splitting.
Parts are staged, synced, then published without clobbering. Free-space preflight
estimates twice the input size plus its normal margin; it is not a reservation.
Publication of multiple filenames is not atomic: parts can briefly be visible.
Failure attempts rollback of owned parts. On Windows, deletion follows the owned
file handle even if its pathname was moved or replaced by another process.
Rollback failure is reported explicitly. No journal guarantees recovery after
power loss; manually inspect leftover parts after a crash.

Official UnRAR supplies RAR4/RAR5 volume switching and missing-volume callbacks.
Start from the first RAR part; opening an arbitrary later RAR part is not promised.
Extraction still rejects links before creating the output directory.

Validation includes plain/encrypted split-7Z round trips, opening `.002`, missing
parts, collisions, cancellation rollback with a concurrent pathname replacement,
and real RAR4/RAR5 multipart fixtures. RAR fixtures are restored locally using
`scripts/fetch-rar-fixtures.py` and are excluded from Git and release packages.
The observed native RAR code-15 race was traced to UnRAR's process-global mutable
`ErrHandler`, reset/read by overlapping decoder calls. The adapter serializes
complete in-process decoder lifetimes with a mutex. The concurrent regression
now runs 120 open/list/test sequences across six RAR4/RAR5, encrypted and multipart
fixtures. This does not assert safety across unrelated external processes.
The packaged verifier also concatenates created
parts and extracts them through independent `py7zr`.
