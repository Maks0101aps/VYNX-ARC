# 0.6 development evidence

This is an incremental development record, not release acceptance. Versions remain
0.5.0 until a validated 0.6 candidate is ready.

## Selected extraction

Moved destination path inspection after selection filtering. Full archive plan
validation and decoding/CRC checks of unselected payloads are retained. A regression
checks that an unrelated destination ancestor which is a regular file neither blocks
selected extraction nor gets modified.

First three-trial release benchmark on the same 100,000-entry ZIP: selected extract
median **1.735 s**, versus **40.141 s** in the 0.5 report. Raw results:
[JSON](benchmarks/0.6/many-core-first.json). This is core timing, not complete GUI
latency. Other Rust checks overlapped part of this run; opening was 1.369 s versus
1.221 s previously (+12.2%), so isolated comparison is required before acceptance.
The harness also passed cancellation and its existing 100 small-archive retention
cycles (137 handles before/after); these are not the planned complete GUI cycles.

Validation: workspace Clippy with warnings denied; all 78 Rust tests passed,
one opt-in benchmark ignored by the ordinary suite; release benchmark passed.

## Isolated reads

Added the owned worker binary, bounded versioned transport, zeroizing request
credentials, metadata fingerprint checks, progress and cancellation, two-worker
admission, Windows job memory/process/lifetime limits and timeout cleanup. GUI
open/preview/materialization/test/entry hashing now use this path. Packaging and
CMake include the worker. Writes remain local pending durable publication recovery.
See [protocol and current boundaries](WORKER_PROTOCOL.md).

Validation: complete source check (Rustfmt, Clippy, workspace tests, dependency
audit, release GUI build, all three CTests) passed. Then strengthened request sending
so a child blocked before reading cannot block timeout monitoring; reran Clippy,
four real-worker integration tests and two transport/job unit tests successfully.
The job test proved a 512 MiB allocation is refused under a 256 MiB committed-memory
cap and that closing the job terminates its waiting child. The diagnostic child
is an ignored test-only entry, absent from shipping binaries. GUI crash injection,
measured resource profiles and complete write isolation remain release work.

## Complete queue descriptions

Journal schema 2 retains up to one million selection names in content-addressed
payload files (16 MiB per description, 64 MiB live aggregate). Each payload is
checked against its recorded size and SHA-256 before recovery. The compact journal
keeps its 1 MiB/96-record bound; writes use QSaveFile without direct-write fallback.
Schema 1 loads without silently repairing its incomplete-selection flags and is
migrated on a subsequent successful save. Unsupported/damaged originals are retained.
New payloads are committed before the referencing journal; obsolete verified payloads
are pruned only after the replacement journal commits. Storage overflow refuses
persistence rather than silently truncating a selection.

Interrupted active writes now recover as `unknown`, which cannot enter generic
retry or completed-history cleanup. Pending jobs recover as explicitly reviewable
interrupted requests. Queue counters distinguish pending/running/cancelled/review.
Publication-intent recording and destination reconciliation are the next step;
unknown writes currently require destination review rather than automatic retry.

Validation: release GUI build and all three CTests passed, including new full
100,000-name recovery, tampered-payload rejection with original-journal preservation,
legacy incomplete-selection migration and a real killed-process recovery test.
