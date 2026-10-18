# Archive worker protocol (development)

The GUI launches its sibling `vynxarc-worker.exe`. Missing workers or failure to
apply Windows job limits stop the request; there is no in-process fallback.
At this stage open, text/byte preview, materialization, archive testing and entry
hashing run remotely. Creation, extraction, modification and replacement still
execute locally until publication recovery is implemented. Do not claim complete
process isolation yet.

Transport uses anonymous inherited stdin/stdout pipes, with no password arguments
or password environment variables. Request and encoded frame credentials use
zeroizing buffers. Worker stderr is discarded and crash dialogs are suppressed.
This does not promise exclusion of credentials from OS-managed dumps/pagefiles.

Each frame starts with eight bytes `VYNXWP01` and a little-endian u32 payload size.
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
