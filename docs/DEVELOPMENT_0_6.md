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

## Controlled writes and publication identity

Protocol 2 moves extraction/creation/split creation/modification/replacement into
workers and forwards overwrite conflicts without blocking parent supervision. Before
archive/volume publication the GUI must atomically record expected new/prior raw
SHA-256 identities. Acknowledgement failure preserves the unpublished archive.
Unknown outcomes are kept for explicit read-only reconciliation; no write resumes
or retries automatically. Per-file extraction recovery remains manual.

Validation: all seven real-worker integration tests passed (including rejection
before commit, original preservation, remote 7Z overwrite decision, stale replies
and timeout of a writer). Clippy passed. Release GUI plus all three CTests passed;
the GUI smoke now uses a real temporary persistent journal. Qt also checks durable
publication identity roundtrip and rejects presentation hash reports as identities.
The first GUI attempt timed out after this identity-format defect caused a safe
publication refusal; corrected code and repeat results are recorded here rather
than treating that attempt as a pass. Forced before/after-commit fault injection
and final-package recovery acceptance remain outstanding.

A four-worker, one-hour-per-worker native parser mutation campaign is running on
frozen binaries under `.dev/milestone-0.6/fuzz-campaign-01`; results are not claimed
until it finishes. The harness records its aggregate active wall-time definition,
resource bounds, individual input hashes and retained unexpected failures. This is
mutation testing, not sanitizer or coverage-guided fuzzing.

## Conversion, batch retry and working copies

Added stream conversion for supported inputs into new ZIP/7Z containers. A separate
readback verifies normalized names/sizes/directories and every decoded file SHA-256
against hashes collected from the original stream (or the verified edited copy).
The source is read-locked and preserved; output publication never overwrites an
existing file. Encryption removal requires explicit consent. Representable modified
times are transferred; ZIP has DOS timestamp precision. Comments/ACLs/ownership and
extra fields are not preserved. No loose plaintext conversion workspace is created.

The GUI reads a preliminary metadata plan asynchronously, shows decoded size and
errors, and offers format/compression/resource/encryption/name-conflict choices.
Single-output names are editable. Passwords stay in memory; protocol 3 carries
source/destination credentials separately. Batch retry chooses only failed jobs,
retains original errors and links new attempts through retryOf. Restoring selected
names now uses an index rather than a quadratic scan of the complete archive.

Working copies now live in a dock with size/error/last-verification details, an
explicit content check and Save as new archive. Admission uses a 2 GiB budget and
32-copy bound; editor growth beyond the budget never deletes an existing edit.
The original archive and working file are preserved by Save as. Container-change
and concurrent-editor-change errors now explain their different causes.

Validation: four core conversion tests include the format/encryption matrix,
folders, RAR4/5, Save as, explicit-decryption refusal, collision, stale edits and
cancellation; eight real-worker integration tests passed. Clippy passed. Release
GUI and all three CTests passed, including actual conversion-plan reads, collision
handling, working-copy quota and Save as through the real GUI queue and persistent
journal. CLI ZIP/7Z/TAR conversions were also read successfully by Python's independent
ZIP implementation. Extended fault injection, editor-host tests, other independent
readers and final release acceptance remain pending.

## Actual editor hosts

[First editor report](verification/0.6/real-editors-first.json): VS Code **1.142.0**
and LibreOffice Writer **26.8.1.1** each changed/saved four real working copies and
verified saveback into plain/encrypted ZIP and 7Z. Writer changed the underlying
file identifier in all four saves, covering its replace-through-temporary-file path.
Client preparation and saveback were separate processes. Working copies survived
and their bytes exactly matched decoded saved entries and publication receipts.
These are actual editor API workflows (VS Code extension host and headless Writer
UNO), not manual editor UI testing. They are separate from the synthetic PowerShell
write in the ordinary GUI smoke. Test credentials are public fixed fixture strings;
passwords were not placed in process arguments or acceptance manifests.

The optional editor driver requires explicitly named local editor paths. Writer was
administratively extracted into an ignored workspace directory from the official
26.8.1 MSI, checked against its published SHA-256 and a valid Document Foundation
Authenticode signature; it was not installed globally. Each editor used a private
profile and owned fixture files. Raw logs remain under the local evidence root.
