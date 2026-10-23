# 0.8.0 follow-up — 2026-10-23

Status: release candidate. Clean-Windows acceptance remains blocked by the host;
full manual nested/copy/recovery keyboard acceptance has not been attested.
This report supplements, rather than replaces, earlier evidence.

## Source and package identity

Fresh `scripts/check.ps1`: **95 Rust tests passed, 2 ignored**, fmt, strict
Clippy and audit (1350 advisories / 109 dependencies) passed; CTest **5/5**.
`dispatchSpec(const ArchiveJobSpec&, ...)` performs centralized validation and
uses typed execution parameters. The built GUI hash equals the packaged-stage
GUI hash: `453da11d5a7c8b5b9ea04151ef101210b9756b9499a81a74e674a5712d9645b1`.
No application binary was modified by this follow-up.

Package hashes and the structured follow-up are recorded in
[results.json](verification/0.8/final-followup/results.json).
CI must pass on the commit containing this source tree; older green CI is not
acceptance of this candidate. No release tag or publication is part of this work.

## Narrator and language review

The user explicitly confirmed that Tab/Shift+Tab navigation, button names and
states, settings through Ctrl+comma, temporary-files table/buttons, and Escape
focus return work and are spoken by Windows Narrator. This is a basic interactive
smoke check, not certification of every control or a complete recovery workflow.

The agent reviewed the English source and Ukrainian/Russian translations of
`ExtractionRecoveryDialog`, `InterArchiveCopyDialog` and `TemporaryStorageDialog`.
No clear semantic error was found in that sample. Catalog placeholders match;
both catalogs have 572 messages and no unfinished entries. This does not claim
a human review of the full language/theme/DPI matrix.

Remaining manual scenario: open an archive, enter/leave a nested archive, select
files, copy them to another archive, resolve a conflict, cancel an extraction,
review its recovery plan, and inspect temporary files using only the keyboard.
Listen for the selected table row, error/status text and focus after each dialog.

## 7Z memory decision

Accept the measured Balanced-mode resource cost as an explicit 0.8.0 limitation.
The historical difference from roughly 103 MiB remains unexplained; it is not
reported as fixed or as proof that no leak exists.

Reproduction uses the packaged CLI, one deterministic 16 MiB random file,
three trials per resource mode, 10 ms RSS samples and independent `py7zr`
extraction followed by SHA-256 equality. All **9/9** round-trips passed.

| Mode | Median sampled RSS | Median wall time | Encoder settings |
| --- | ---: | ---: | --- |
| Eco | 34.0 MiB | 3.094 s | LZMA2 level 5, 2 MiB dictionary, 1 thread |
| Balanced | 218.5 MiB | 1.983 s | LZMA2 level 5, 8 MiB dictionary, 2 threads |
| Maximum | 218.8 MiB | 2.050 s | LZMA2 level 5, 8 MiB dictionary, up to 8 threads |

Balanced was about 36% faster than Eco on this input, with a larger workspace.
Its sampled RSS is below its 384 MiB encoder-workspace estimate. That estimate
is not a process RSS cap. Maximum's similar result on this small input does not
predict its peak on larger inputs. GUI worker committed-memory limits remain
separate; direct CLI commands are not implicitly supervised.

This decision preserves codec settings, integrity checks and cancellation.
Use Eco when memory matters more than throughput. Reopen the finding if the
same workload shows growing per-run peaks, violates worker limits, or fails
independent round-trip verification. A controlled old/new binary comparison
is still required to explain the historical 103 MiB measurement.

```powershell
python scripts/benchmark-7z-memory.py --cli dist/stage-<id>/vynxarc-cli.exe `
  --output docs/benchmarks/0.8/7z-resource-matrix-new.json
```

Generated corpora and detailed benchmark samples are local, ignored data.
The compact results above remain versioned.

## Clean Windows: blocked before guest execution

The user confirmed Windows Sandbox fails to initialize with:
`No more data is available. (0x80070103)`.
Read-only host checks: firmware virtualization and SLAT are enabled, but
`Win32_ComputerSystem.HypervisorPresent` is **false**. Hyper-V VM enumeration
also denied access. No guest report was produced; no clean-machine pass is claimed.

`scripts/check-clean-windows.ps1` now checks whether the hypervisor is running
before launch and records `BLOCKED_HYPERVISOR_NOT_RUNNING` instead of treating
process creation as success. It does not modify boot/security settings or reboot.
See Microsoft's [Sandbox installation prerequisites](https://learn.microsoft.com/en-us/windows/security/application-security/application-isolation/windows-sandbox/windows-sandbox-install).

Prepared payload: `.dev/final-08-clean-official/acceptance.wsb`.
After the host can run Sandbox, open it and retain `evidence/results.json` and
guest logs. The existing guest script checks hashes, SDK absence, portable
ZIP/7Z round-trips, installation, Open With, settings retention and uninstall.
Host-only execution of a smaller portable smoke harness passed, but is explicitly
excluded from clean-Windows acceptance.

## Additional observed compatibility limitation

A small ZIP using Store and containing another ZIP can be rejected with
`CORRUPT: Missing or ambiguous ZIP end records`. Reproduced through the packaged
GUI and CLI: the nested payload contains another EOCD signature inside the
preflight search window. `core/src/parser.rs` intentionally rejects ambiguity to
prevent backend fallback to unbounded metadata. This protection is unchanged.
Supporting this case requires a separate safe parser design and regression tests;
do not remove the ambiguity check just to make this fixture pass.
