# Archive worker protocol (development)

The GUI launches its sibling `vynxarc-worker.exe`. Missing workers or failure to
apply Windows job limits stop the request; there is no in-process fallback.
Open, preview, materialization, testing, entry hashing, extraction, creation,
split creation, modification and edited-entry replacement run remotely. Plain-file
verification remains a bounded streaming hash operation in the GUI core. CLI commands
retain direct execution for automation; `isolated-list`, `isolated-test` and `isolated-extract` select supervised execution;
`convert` always uses workers. Protocol 3 adds a separately zeroizing destination
password, so conversion never reuses source encryption settings implicitly.


Transport uses anonymous inherited stdin/stdout pipes, with no password arguments
or password environment variables. Request and encoded frame credentials use
zeroizing buffers. Worker stderr is discarded and crash dialogs are suppressed.
This does not promise exclusion of credentials from OS-managed dumps/pagefiles.

Each frame starts with eight bytes `VYNXWP03` and a little-endian u32 payload size.
Requests are limited to 16 MiB, responses to 128 MiB, individual strings to 1 MiB,
metadata/selection arrays to 1,000,000 elements, preview blobs to 16 MiB. Decoders
reject unknown versions, truncated/trailing fields, malformed bools/UTF-8 and
impossible counts before reserving from peer-provided counts. Replies use a bounded
channel; one process handles one request and then exits. Metadata fingerprints
reject stale entry IDs after reopening the archive. This fingerprint is a metadata
check, not a cryptographic snapshot of the complete archive bytes.

Progress messages arrive at most every 100 ms. Cancellation is forwarded through
the same private input channel; a nonresponsive child is terminated after two
seconds. Requests currently have a 30-minute wall budget and the application has
at most two workers. Resource profiles provisionally cap worker committed memory
at 768 MiB / 1536 MiB / 3072 MiB. These are process caps, distinct from encoder
workspace settings, and require corpus peak measurements before release acceptance.

Windows jobs apply process memory, one-active-process and kill-on-close limits
before the worker receives its first request. Completion-port memory notifications
produce a specific error when delivered; unexpected termination without that
notification remains an honest combined crash/possible-memory-limit message.
Notifications are not guaranteed by Windows, so missing notifications never prove
that memory was below its cap. Jobs do not restrict the user's filesystem rights.

References: [job objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects),
[extended limits](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_extended_limit_information),
[completion notifications](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_associate_completion_port).

## Publication acknowledgement (protocol 3)

A worker computes raw SHA-256 identities of each verified staged archive/volume
and the existing destination before committing it. It sends a publication notice
and waits for an explicit acknowledgement. The GUI records the intent through its
atomic journal before acknowledging. Failure to record it rejects publication.
Passwords and arbitrary captures are excluded from the journal.
When the user disables history, the journal and publication receipts remain in
memory only: current-session verification still works, but queue recovery after
process exit is intentionally unavailable. Retained working files are separate.

The parent keeps monitoring while publication/conflict decisions are pending.
Conflict replies and cancellation use bounded control frames. Unexpected loss of a
writer, forced cancellation, malformed replies and time/memory termination report
`RESULT_UNKNOWN`; they never promise that the original survived. The GUI also treats
any error following a recorded intent as uncertain. An ordinary successful commit
has no cancellable post-publication verification in the worker.

The queue's Verify result action reads destination SHA-256 values in workers. All
expected result hashes matching establishes completed publication; all prior hashes
(or prior absence) matching establishes that the destination is currently unchanged.
Mixed or inaccessible results remain unknown. There is no automatic retry. Working
copy recovery metadata is updated only for a recorded edited-save input digest;
the current working file is independently hashed and newer bytes remain dirty.
Extraction has
per-file publication and no aggregate receipt yet; interrupted extraction requires
manual destination review and is not automatically retried.

The legacy direct CLI has no durable journal; the acknowledgement requirement is
controlled by Operation and enabled by the GUI dispatcher. Direct core callers can
explicitly enable it. This boundary is tested separately from normal CLI execution.
