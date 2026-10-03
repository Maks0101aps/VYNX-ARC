# Post-0.2 development work in progress

This work uses branch `feature/0.3-development` in
`C:\Users\Maksi\Documents\projects\VYNX-ARC-development`, based on
`9e759c0517f7979530ae2d14008163333671fd08`. Public version is still **0.1.0**.
The original `VYNX-ARC` checkout and its dirty investigation files are preserved.
`checkpoint/blocked-release-20261003` retains the base commit; the original
`.dev/blocked-release-snapshot-20261003T155121Z` contains the tracked binary patch,
working-file ZIP and SHA-256 manifest for the then-uncommitted blocked state.
The original checkout subsequently recorded those eight investigation files in
commit `a8c173b23511da4100ab46f51e6c13391037beb3` (`docs+scripts`). Their hashes
still match the recovery manifest; this feature work did not change them.

**0.2.0 acceptance remains B / blocked. Root cause: STILL UNKNOWN.** The Pylance
causal OFF experiment is pending. This branch changes neither ZIP replacement
publication nor its single-attempt retry behavior. RELEASE_ACCEPTANCE and other
release evidence are preserved, not refreshed to imply a passing release.

## Actual preset mapping

| Format | Store / None | Fast | Balanced | Maximum |
| --- | --- | --- | --- | --- |
| ZIP | Stored | Deflate 1 | Deflate 6 | Deflate 9 |
| 7Z | Copy | LZMA2 1, 1 MiB dictionary | LZMA2 5, 8 MiB dictionary | LZMA2 9, 32 MiB dictionary |
| TAR | Store | Rejected | Store (default compatibility) | Rejected |
| GZIP / TAR.GZ | Deflate level 0 | Deflate 1 | Deflate 6 | Deflate 9 |
| XZ / TAR.XZ / LZMA | Rejected | LZMA 1, 1 MiB dictionary | LZMA 5, 8 MiB dictionary | LZMA 9, 32 MiB dictionary |
| BZIP2 / TAR.BZ2 | Rejected | Level 1 | Level 6 | Level 9 |
| ZSTD / TAR.ZST | Rejected | Level 1 | Level 3 | Level 19 |

Resource policy clips these dictionaries and supported worker counts. XZ uses
LZMA2 inside the XZ container; legacy LZMA uses LZMA1. GZIP level 0 still uses a
GZIP/Deflate container. ZIP does not expose LZMA/ZSTD write modes or fake parallel
compression. Selected format must match the output suffix. Unsupported encryption,
Store and standalone folder/multiple-input combinations fail before publication.
ZIP/7Z AES and split 7Z remain available; new formats do not add encryption/splits.

## Resource policy

| Setting | Eco | Balanced | Maximum |
| --- | --- | --- | --- |
| 7Z decoder worker cap | 1 | 2 | 8 |
| Supported encoder thread cap | 1 | 2 | 8 |
| LZMA dictionary / ZSTD encoder window cap | 2 MiB | 8 MiB | 32 MiB |
| LZMA encoder workspace planning budget | 64 MiB | 384 MiB | 1024 MiB |
| XZ/LZMA decoder memory limit | 64 MiB | 384 MiB | 1024 MiB |
| ZSTD decoder maximum window | 8 MiB | 64 MiB | 128 MiB |
| BZIP2 compression level cap | 3 | 9 | 9 |
| Windows calling worker priority | Temporarily below normal | Unchanged | Unchanged |

Thread counts also respect available CPUs. LZMA encoder selection uses an
estimated per-thread workspace of `14 * dictionary + 16 MiB`; this is a scheduling
estimate, **not a process RSS limit**. Backend buffers, chunks, archive metadata
and verification also consume memory. Eco lowers only the calling worker when
possible and restores its priority on scope exit, including errors/cancellation.
Maximum does not elevate OS priority. Decoder worker caps do not imply parallel
GUI jobs. ZIP/GZIP/BZIP2 and legacy LZMA encoding are single-threaded; 7Z/XZ/ZSTD
use codec threading where supported. Small files can use less than the cap.

Creation shows effective codec, level, dictionary/window, thread count and budget
in the dialog and CLI. Operation details show the applied priority where available.
GUI creation remembers the chosen resource mode for subsequent operations; the
CLI accepts `--resource eco|balanced|maximum` and creation also accepts
`--preset store|fast|balanced|maximum`.

## Operation behavior

QtConcurrent workers perform archive work. Progress polling is every 100 ms.
The panel shows phase, current file, processed/reliable total bytes, elapsed time,
an exponentially smoothed two-second throughput estimate and cancellation state.
ETA requires three seconds of progress and more than 1% completion, resets on
phase/total/counter changes and uses a 25% margin. Unknown totals are indeterminate.
ETA is an estimate for the current byte phase, not a promise about publication or
future phases. Cancellation remains cooperative and cannot interrupt an already
executing native commit or every codec-internal computation immediately.

The first queue is an in-process FIFO for incoming open/Explorer requests while
another job runs. It has 32 pending slots and trims displayed completed history
to 64 items. The list exposes pending/running/completed/stopped/cancelled states;
the pending cancellation button removes the request and releases captured inputs
and secrets. All jobs, including reads, run serially. This conservatively protects
same-archive writes and avoids unsafe alias/concurrent decoder assumptions.
Toolbar actions remain disabled during work; general multi-job scheduling and
parallel independent reads are deferred. No daemon, persistence or background
service is created. Closing the window cancels active work and drops pending work.

## Added formats and safety

GZIP/XZ/BZIP2/ZSTD/LZMA standalone streams support browse/test/extract/create;
standalone creation takes one regular file. TAR.XZ/TAR.BZ2/TAR.ZST support directory
trees via the existing checked TAR writer and extraction pipeline. Standalone
metadata scans the whole decoded stream, so opening it is not constant-time.
Output names derive from the archive filename. New decoder paths check cancellation,
default decoded-size/ratio limits, and XZ/LZMA memory or ZSTD window limits before
unbounded decoding. TAR metadata rejects oversized entry sizes and drains codec
trailers after the TAR end marker. Archive path/link/collision/MOTW protection,
verified staging and no-clobber creation remain in use. No codec convenience
unpack API is used. RAR stays read-only; CAB/ISO/WIM are not implemented.

## Validation and limitations

On Windows 11 x64/MSVC, `scripts/check.ps1` passed fmt, strict Clippy, **59 Rust
tests**, dependency audit (107 dependencies; no reported advisories), native build
and all **three CTest targets**. Presentation tests cover real codec display,
unknown-total/cancellation UI and bounded FIFO ordering/capture cleanup. GUI smoke
tests actual asynchronous queue dispatch and cancellation of a pending invalid
request, in addition to existing archive/conflict/hash/secret-lifetime behavior.
Rust regressions cover every ZIP/7Z preset/resource combination, eight new format
round trips, unsupported combinations, cancellation, oversized LZMA dictionary,
zero-filled standalone streams, compressed TAR trailer corruption and restoration
of Windows Eco priority. These tests do not establish a hard total-memory ceiling.

Fourteen deployed light/dark English screenshots were generated for creation,
extraction, conflict, settings, about, operation and breadcrumb presentation in
`.dev/development-ui`. Creation and operation views were visually inspected;
operation captures are explicit display fixtures, not timed live workloads.

ZIP/7Z measurements and artifact identities are in [PERFORMANCE.md](PERFORMANCE.md).
No 7-Zip/WinRAR superiority claim is made. New-format benchmarks, broad producer
interoperability, hostile metadata allocation auditing, concatenated/skippable
frame combinations, Unicode normalization collisions, hard RSS caps, parallel
GUI reads, long-operation ETA qualification, other platforms and clean-VM release
acceptance remain unverified. The development branch does not resolve the normal
project-location ZIP publication failure.
