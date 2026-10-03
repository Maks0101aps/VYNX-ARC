# Proposed Windows publication handling — design only

No production implementation is authorized in this pass. The current package
still makes one `NamedTempFile::persist` attempt. This proposal is conditional on
the causal controls recorded in [PUBLICATION_ROOT_CAUSE.md](PUBLICATION_ROOT_CAUSE.md).

## Error classification

| Native failure at the publication stage | Proposed treatment |
|---|---|
| `ERROR_SHARING_VIOLATION` (32) | Candidate for bounded waiting at the final rename, retaining the already verified replacement. A sharing conflict can persist; success is never assumed. |
| `ERROR_LOCK_VIOLATION` (33) | Candidate only when the failed final publication is shown to encounter a competing byte-range lock. Do not retry unrelated read/rebuild/extraction failures under this policy. |
| `ERROR_ACCESS_DENIED` (5) | Fail immediately by default. The code also represents permission/ACL/read-only and other denials, so it is not a generic transient signal. |
| Other errors | Fail immediately with the native reason and original-preservation result. |

Microsoft defines [32 as a sharing conflict, 33 as a portion-of-file lock, and 5
as access denied](https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--0-499-).
The captured field failure is **5**, not 32 or 33. The captured stack identifies
MoveFileExW, but the current `persist` error interface also includes its temporary
attribute step. An error number alone cannot identify the step or competing actor.
MoveFileExW replacement remains subject to [security requirements](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw).

Repeated successful OFF controls and returned ON failures would establish a
workstation-level cause. They would **not** make every future error 5 retryable.
Allowing 5 into an automatic retry path additionally needs a reliable,
non-privileged runtime classification of the exact final rename failure as a
competing-handle conflict, without clearing attributes or changing ACLs. Neither
ProcMon availability nor a stored list of process names is such a production
classifier. Read-only/ACL checks can exclude some denials but cannot prove all
remaining denials are transient, and a successful DELETE-access probe did not
exclude the captured interference.

**Recommendation with the current API/error information: do not automatically
retry error 5.** Keep the native denial visible. If a safe classifier cannot be
specified and validated, require an explicit user retry after resolving competing
applications. This leaves the automatic handling of the observed error unresolved
rather than disguising it as a proven transient failure.

## Bounded transaction for a qualified conflict

The following is a proposed policy, not behavior already implemented:

1. Keep ownership of the synced, independently verified sibling replacement,
   including its propagated MOTW. Do not rebuild, delete the destination, truncate
   it, or copy over it between attempts. A persist failure retains the staging
   file through its returned owner.
2. Keep MoveFileExW/NamedTempFile publication. Before each qualified reattempt,
   revalidate pinned ancestors, reject changed source identity/content and
   validate that staging is still the verified replacement. Changes stop the
   transaction, not trigger a fresh overwrite.
3. Use a monotonic **one-second maximum waiting budget**, with delays of
   **20, 40, 80, 160, then at most 200 ms**. Clip each delay to the remaining
   budget. Issue no attempt after the deadline; the first unqualified error ends
   the policy immediately. The waiting budget does not promise interruption of
   a synchronous Windows syscall already in flight.
4. Check cancellation before waiting, during interruptible waiting and
   immediately before each native commit attempt. Cancellation discards staging
   and leaves the original intact. Once the native rename succeeds, report
   committed success even if cancellation arrives concurrently.
5. At exhaustion, report the final native error and an actionable generic
   explanation when a competing application has been established:

   > VYNX ARC couldn't update this archive because another application is using it.
   > Close applications that may be scanning, previewing, indexing or editing the
   > archive, then try again.

   Do not name Pylance. For unclassified error 5, include the access-denied reason
   and permission/read-only checks instead of asserting that another application
   is definitely responsible.

Before implementing this design, validate transient and persistent target handles,
error 32/33/5 classification, ACL/read-only immediate failures, cancellation during
waiting, changed-original detection, staging cleanup and original hash preservation.
The existing `.dev`/TEMP and MOTW acceptance matrices remain required afterward.
No alternate Windows publication API is recommended solely to alter timing.
