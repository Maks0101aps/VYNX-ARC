# 0.2.0 engineering acceptance: B — still blocked

Recorded 2026-10-03 on the existing Windows 11 development workstation.
Version stays **0.1.0**. No version bump, feature expansion, push or deployment.

Git HEAD: `220fbff9e3cc7ddaa78b3ea8cd1cba17da370b44` on `main`.
The initial tracked worktree was clean. Recoverable source checkpoint:
`checkpoint/pre-020-20261003`. The pass's changes remain uncommitted for review.
Raw logs/results are local generated evidence under `.dev` and TEMP, not historical
claims promoted to current acceptance.

## ZIP replacement evidence and fixes

[Reproducer and exact capture procedure](PUBLICATION_DIAGNOSTICS.md).
The original package passed 3/3 fresh full runs with its retry loop enabled;
those successes did not prove the error absent. After removing retries, the final
package failed full verification **5/5 in `.dev`**, preserving failure reports.
Its full-sequence reproducer also failed **5/5 in `.dev`**, while all nine smaller
controls passed 5/5 each. All 50 sequence cases passed in TEMP.

Prefix bisection in `.dev`:

| Prefix before ZIP rename | Passed / failed |
| --- | --- |
| ZIP lifecycle | 5 / 0 |
| ZIP + 7Z lifecycles | 5 / 0 |
| Then TAR create only | 5 / 0 |
| Then TAR create/test | 4 / 1 |
| Then TAR create/test/list | 3 / 2 |
| Then TAR extraction | 4 / 1 |
| Then TAR Smart Extract | 4 / 1 |
| Complete verifier prefix including TAR.GZ and repeated RAR | 0 / 5 |

The smallest tested failing prefix is through TAR test. This does not establish
TAR as the cause: elapsed time/concurrent activity remains a confounder.

Failure-time Restart Manager repeatedly reported the CLI and Code.exe PID 26548.
Its command line identifies the Pylance language server. An external kernel
snapshot caught Pylance's read handle to the exact target ZIP (matching file ID).
CLI snapshots contained only the expected temporary file and pinned parent,
with no source ZIP/ADS handle or matching mapped-file region. All observed
file-type handles were inspectable; unsupported duplicate-handle queries were
recorded for another object type. DELETE-access probes succeeded at snapshot time.
Thus concurrent Pylance access is proven; its role in the precise denial is
**not yet proven**. A transient sharing/filter interaction remains unresolved.

No AccessDenied root-cause fix is claimed. The publication API is unchanged.
Retries and sleeps were removed to expose the first error. Verbose stage/API
diagnostics and optional MOTW-skipping are behind an explicit build feature;
normal packages do not contain these diagnostic paths.

A separate physical long-path defect was fixed: a 293-character plain destination
failed creation with OS error 3, while its extended Windows form succeeded.
Existing publication parents are now canonicalized to extended paths for sibling
staging and native publication, retaining ancestor/reparse and no-clobber checks.
The new encrypted, Unicode, physical-long-path/MOTW regression failed before this
fix and passed afterward. Direct primitive tests cover flushed sibling replacement,
cancellation immediately before publication, read-only rejection and cleanup.

## Final packaged stress matrix

Every category has a separate archive; no failed operation is retried.
"Completed" counts successful operations/cycles before the first failure.

| Category | `.dev`, no MOTW | `.dev`, MOTW | TEMP, no MOTW | TEMP, MOTW |
| --- | --- | --- | --- | --- |
| 100 consecutive adds | FAIL after 15 | FAIL after 10 | 100/100 PASS | 100/100 PASS |
| 100 consecutive deletes | FAIL after 5 | FAIL after 7 | 100/100 PASS | 100/100 PASS |
| 100 consecutive renames | FAIL after 11 | FAIL after 10 | 100/100 PASS | 100/100 PASS |
| 100 mixed cycles (add/rename/delete/restore rename) | FAIL after 2 | FAIL after 2 | 100/100 PASS | 100/100 PASS |

All eight project failures preserved the prior archive manifest and file bytes.
TEMP completed 1,400 publication operations across both MOTW states, with content
checks after every operation. Earlier failing stress runs are retained; successful
controls do not replace them. Diagnostic stress also failed every project category.

Controlled full-prefix MOTW matrix: absent/present/independently staged ADS/
diagnostically skipped propagation each failed 3/3 in `.dev` and passed 3/3 in
TEMP. Failures without ADS show that MOTW propagation is not necessary for the
denial. Skipped propagation is diagnostic evidence only, not product acceptance.

Environment comparison: both workspaces use the same healthy C: NTFS volume and
owner, with inherited ACLs. Directory attributes show no compression, encryption,
NoIndex or reparse/cloud flags; their recorded OneDrive root is outside these
paths. Base lengths are 47 and 33 characters. Effective process integrity is
Medium. Defender real-time protection is enabled and CFA is disabled. These
facts do not attribute the denial to an ACL, indexer or antivirus. Directory
mandatory labels/complete filter stacks are not established by this snapshot.

## Source gates, RAR and security

| Gate | Current result |
| --- | --- |
| `cargo fmt --all -- --check` | PASS |
| `cargo clippy --workspace --all-targets --locked -- -D warnings` | PASS |
| Diagnostic features: strict Clippy with `--all-features` | PASS |
| `cargo test --workspace --locked` | 50 passed, 0 failed |
| Native MSVC CMake build | PASS |
| CTest | 3/3 passed: presentation, shell smoke, GUI smoke |
| cargo audit | PASS, 1,288 advisories loaded; known RustSec findings only |
| Python diagnostic/verifier syntax compilation | PASS |
| GitHub Actions execution | NOT TESTED here; workflow now enforces shared gates |

The final all-features Clippy log is `.dev/020-clippy-all-features-final.log`.
An earlier raw PowerShell invocation reported a wrapper error on native stderr;
Cargo itself completed. Its log is retained. The final invocation used the
project's exit-code-aware `Invoke-VynxTool` and exited successfully; no warning
was suppressed or dependency changed.

The existing UnRAR lifetime mutex is retained. Six concurrent callers completed
20 open/list/test sequences each: **120 total**, covering RAR4, RAR5, encrypted
RAR4/RAR5 and both multipart generations. This confirms the in-process regression;
it makes no external-process safety claim.

Security regressions pass for traversal, absolute/UNC/drive-relative paths, ADS,
DOS names, trailing dot/space, control/Bidi characters, duplicate/case and
file/directory prefix collisions, archive links, junction/reparse destinations,
entry/single-file/total-size/ratio limits, missing volumes, changed ZIP metadata,
corrupted payloads, locked replacement/extraction output, and cancellation cleanup.
Encrypted ZIP/7Z content and wrong-password preservation tests pass.
An isolated insufficient-permission fixture rejected the operation and retained
the original SHA-256 after its ACL was restored. No global permissions changed.

Unresolved: Unicode normalization-equivalent collisions, comprehensive parser
allocation budgets, backend/Qt password wiping, and hostile local-process race
proof. No data loss was observed; this is not a comprehensive security certification.

## Final packaged verification and clean Windows

The final portable SHA-256 below was used in both five-run series. Developer Qt
variables were removed and application PATH contained only Windows/System32.

| Application location | Archive workspace | Runs | Result |
| --- | --- | --- | --- |
| `.dev` | `.dev` | 5 | 0 passed, 5 failed at ZIP rename (error 5) |
| `.dev` | TEMP | 5 | 5 passed, 0 failed |

The TEMP series exercised actual deployed VynxArc.exe and vynxarc-cli.exe, all
supported format round trips, RAR4/RAR5/multipart, split-7Z/missing-volume checks,
Smart Extract, file/entry hashing, supplied digests and ZIP/7Z modification with
independent readers. No verified GUI process remained running. Password operations
remain GUI/core tests because the CLI deliberately has no password argument.

`VERIFICATION.json` now aggregates immutable per-series reports for the same
artifact. Its overall status stays failed after the TEMP success; a successful
location cannot mask an earlier failure. Each run's stdout/stderr and JSON survive.

| Clean Windows category | Result |
| --- | --- |
| Portable, GUI/CLI, create/modify/extract without developer installations | NOT TESTED on a clean VM; workstation isolation passes in TEMP |
| Installer installation, Open With and uninstall | NOT TESTED |
| Background-process/startup-entry cleanup after uninstall | NOT TESTED |
| Signed modern Explorer registration/visibility | NOT TESTED; unsigned identity, no signing configured |

Hyper-V enumeration failed because this account lacks permission. No accessible
clean VM was established. The installer and portable package build successfully;
that is not install/uninstall evidence. Certificate absence is not recorded as an
application failure. No trust certificates or Developer Mode changes were made.

## Artifacts and retained evidence

Artifacts are development candidates, **not accepted 0.2.0 releases**.

| Path | SHA-256 |
| --- | --- |
| `dist/VYNX-ARC-Portable-x64.zip` | `129ff5382aa3f4ca4849d045c9158a802a5e31db9f2b948ef4b9d723d70d3105` |
| `dist/VYNX-ARC-Setup-x64.exe` | `35a4d0908e03f5cccf039361689429cf0f60269c5593c9e5573715c8278e93f8` |
| `dist/VYNX-ARC-Explorer-x64.msix` | `950564cfda61c520ad80ba110e62a312d2bb9357a306bdd21013bc43c6a12e38` |

Runnable staging: `dist/stage-3c7abc2792d04418a44cd61a5364d4f6`.
Checksums: `dist/SHA256SUMS.txt`. Normal binaries were built without diagnostics.
The earlier failed long-path release gate is retained in `.dev/020-release.log`;
the successful build/installer log is `.dev/020-release-accepted-source.log`.
The final source check after the additional changed-ZIP regression is
`.dev/020-final-check.log` (50 Rust tests and 3 CTest targets). Production source
and artifact hashes are unchanged by that test-only addition.

Key raw evidence paths:

- Final packaged project series: `.dev/verification-series-1jh0or_l`.
- Final packaged TEMP series: `.dev/verification-series-ttxbzvw1`.
- Final sequence cases: `.dev/zip-publication-1lhwzjdl` and
  `%LOCALAPPDATA%/Temp/zip-publication-507ou5gw`.
- Final project stress: `.dev/zip-stress-_ihi23l1`.
- Final TEMP stress: `%LOCALAPPDATA%/Temp/zip-stress-nmadqy00`.
- Diagnostic failure matrix: `.dev/zip-stress-mg4jxoy3`.
- Exact external target-handle capture: `.dev/zip-stress-3yx70mhu`.
- Prefix/stage/MOTW summaries: `.dev/020-prefix-matrix.log`,
  `.dev/020-tar-stage-matrix.log`, `.dev/020-zone-matrix.log` and their per-case logs.
- Environment: `.dev/publication-environment.json`.
- Latest permission case: `%LOCALAPPDATA%/Temp/vynx-permission-0f7fb9f82d054340812dace098a1dd34`.

Documentation corrected: SUPPORTED_FORMATS, VOLUMES, IMPLEMENTATION_STATUS,
UI_QA, UI_DESIGN, DEPENDENCIES, THIRD_PARTY_NOTICES, MODIFICATION, BUILD,
SECURITY_MODEL, CHANGELOG and README. Historical measurements/counts remain
explicitly historical. PUBLICATION_DIAGNOSTICS and this report supply current status.

**Decision B:** unresolved AccessDenied, failed project stress/publication and
unexecuted clean-environment acceptance prevent 0.2.0 acceptance. Version remains
0.1.0. The next required evidence is the prepared operation/share-mode/stack capture,
not another passing retry or an API substitution.
