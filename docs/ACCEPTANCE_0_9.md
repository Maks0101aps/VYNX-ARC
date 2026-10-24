# 0.9.0 acceptance - development candidate, blocked

Date: 2026-10-24. Base: `release/0.8.0` / `1225285`.
Work branch: `development/0.9.0`; push is authorized for CI qualification. No tag or release publication
was performed. Signing and interactive acceptance follow-up is in progress. This is not a stable 0.9.0 release.

## Implemented scope

Native RAR5 creation uses pinned rars 0.12.0 with real levels 0/1/3/5, optional
solid, dictionary controls, AES-256/data/header encryption, recovery records and
native volumes. Single-volume unlocked/unsigned RAR5 supports verified rebuild
add/delete/rename and working-copy save-back. Reader remains independent UnRAR.
GUI/CXX/CLI, queue/review/retry and worker protocol 4 carry the same parameters.
No external RAR executable is a runtime dependency.

Other changes fix stored nested ZIP end-signature rejection without metadata
fallback, canonicalize RAR separators, preserve typed streaming failures, bound
Windows extraction parent caching and use one anonymous RAR rebuild spool.
The creation dialog scrolls on small/high-DPI screens with reachable buttons.
Settings, schema-1/2 journals and working-copy records remain compatible.
Details and unsupported combinations: [NATIVE_RAR.md](NATIVE_RAR.md).

## Release blockers and unexecuted gates

1. **Historical ZIP error-5 investigation remains bounded.** The previous
   MoveFileExW publication failed in all eight category/MOTW groups and also on
   retained 0.8.0; all original hashes were preserved. The current native
   ReplaceFileW path has repeated passing strict matrices and compatible-reader
   controls. Non-delete-sharing readers still cause a safe refusal. No extra
   waits, retry expansion, process termination or security change was used.
   Final source uses a unique recovery backup, zero metadata-ignore flags and
   retains both payloads on partial native failures. Errors 1176/1177 are tested
   with deterministic failure models, not a claim of kernel fault injection.
   Exact external attribution for the historical failures remains unknown; this
   does not invalidate the independently measured final publication path.
2. **Clean Windows guest unavailable.** Current read-only host check still reports
   `HypervisorPresent=False`; payload is prepared, guest tests have not run.
   No boot/security changes or reboot were performed.
3. **Final-tree GitHub Actions pending.** The workflow is updated with native
   RAR qualification. The user has now authorized push; the exact commit run will be recorded
   after completion. Older remote green
   runs are not acceptance of this candidate.
4. Packages are **unsigned**. Trusted Explorer sparse-MSIX identity registration
   and clean standard-user acceptance remain open. The MSIX is the existing
   Explorer identity package, not a standalone full-application installer.
5. Full manual keyboard/Narrator acceptance of new RAR/error/volume controls has
   not been attested. Automated Widgets and captured EN/UK/RU checks are narrower.

## Completed source and deployed checks

Final native publication qualification: two fresh full project-path matrices,
**2,800/2,800** replacements across all eight category/MOTW groups. Compatible
attribute/read handles passed; a read handle denying deletion was refused with
unchanged original bytes. Retained 0.8.0 failed the same compatible controls.
See [publication-review](verification/0.9/publication-review.md) for the bounded
independent safety review and its partial-failure modeling limitation.

- Canonical `scripts/check.ps1` via release packaging: fmt, strict Clippy,
  **110 Rust tests passed, 2 ignored**, dependency audit passed (1351 advisories,
  113 dependencies); CTest **5/5**. The ignored tests are the private allocation
  child (launched by its parent test) and explicit corpus benchmark entry point.
- Additional protocol-4 malformed/oversize/truncated/old-version regression and
  worker option/encryption/rename tests passed. Workflow regression harness:
  **54 malformed frames**, actual GUI nested/copy/recovery/native-RAR jobs,
  journal migration and working-copy preservation.
- Actual encrypted-source clipboard/MIME copying into ZIP, 7Z and RAR5 targets
  passed through the window handlers, including keep-both conflicts and folders.
  A CLI conversion regression verifies that source credentials never implicitly
  select destination encryption; plaintext conversion requires explicit consent.
- Deployed portable with PATH restricted to Windows/System32 passed GUI smoke,
  native-RAR GUI dispatch, batch/retry, independent ZIP/7Z/Python codec reads,
  RAR4/RAR5/repeated/multipart reads and split-7Z checks. No retained test process.
- Actual per-user **0.8.0 -> 0.9.0** installer upgrade passed: Open With
  registration/removal, installed GUI, settings, schema-1 working copy, unchanged
  file defaults and uninstall. This is host testing, not a clean VM.
- Test-only independent **WinRAR 7.22** validated and extracted **21/21** RAR
  cases with matching content hashes: all presets/resource modes, directories,
  Unicode/empty files, AES, hidden filenames, solid, recovery and real volumes.
  Wrong passwords, damaged headers and missing parts were refused.
- WinRAR repaired deliberate payload corruption using generated recovery data;
  recovered files matched original SHA-256. This qualifies that stored/plain
  recovery case, not every possible damaged/encrypted/multipart repair.
- Windows libarchive independently decoded unencrypted single-volume cases;
  encrypted and volume combinations are explicitly excluded from that fallback
  oracle's qualification scope. Native VYNX decode still runs for all cases.
- **256/256 deterministic mutations passed**; a separate 60-second serial
  campaign on the final CLI passed **285 cases**. These are bounded mutation smoke, not
  coverage-guided fuzzing, sanitizer coverage or a security certification.
- Controlled publication locks **6/6** and forced interrupted rebuild checks
  passed with originals/working copies retained. They do not override strict
  project-path stress failures above.
- Full Apache COPYING bytes, rars/ZIP license metadata and packaged notices were
  checked in the actual portable ZIP. WinRAR is not bundled.

## Failed attempts retained

Initial RAR tests found the native reader's writable-handle sharing conflict;
verification now closes writable RAR container handles. Eco's former 64 MiB
budget refused compression; RAR now has a separately documented managed budget.
GUI creation exposed slash/backslash manifest differences; names are canonical
and nested encrypted working-copy replacement has a regression test.
The nested build environment also hit cmd.exe's PATH length limit; SDK setup now
deduplicates PATH before/after entry and repeated setup held its length at 2872.
An incremental-build check found that CLI-only edits did not trigger the GUI's
post-link sibling copy. An explicit runtime target now synchronizes CLI/worker
after Cargo even when the GUI does not relink; binary equality is checked.

One overlapping source-test launch hit Windows sharing violation before a test
EXE started; sequential gates passed. An initial two-worker fuzz campaign had
two simultaneous timeouts. Both saved inputs replayed in about 0.19 s and the
later strict serial campaign passed. The timeout mechanism is unproven; no
timeout was raised to turn those failures into passes. Their raw reports remain.

The repeated real 20,000-file round-trip used the immediately preceding
CLI/worker before the final publication-only safety correction. Their actual
hashes are retained. The final CLI separately decodes all 20,000 payloads and
final GUI opens that archive. Full 20k timing and 7Z timing measurements were
not repeated after this publication correction; they are preceding-snapshot
measurements, not silently relabeled final-binary timings.

Machine-readable evidence and package identities are in
[verification/0.9](verification/0.9). The ZIP stress script's original
`portable_sha256` field in earlier stress attempts referred to the default old
portable even when an explicit CLI was selected; only their actual `cli_sha256`
identifies the tested binary. The final stress report correctly leaves that
unrelated portable field empty. Baseline/new CLI identities are distinct.

## Artifacts and measured limits

Local files: `dist/0.9.0/VYNX-ARC-Portable-x64.zip`,
`VYNX-ARC-Setup-x64.exe`, `VYNX-ARC-Explorer-x64.msix`, `SHA256SUMS.txt`.
Earlier packages remain under `dist` / `dist/0.9.0-candidate`.
Exact hashes are recorded in [verification/0.9/artifacts.json](verification/0.9/artifacts.json).
Measured performance and corpus boundaries: [BENCHMARKS_0_9.md](BENCHMARKS_0_9.md).

No multi-GiB/above-4-GiB RAR corpus, full metadata preservation, arbitrary
recovery repair, RAR4 writing or multipart modification is claimed. Unicode
normalization equivalence and hostile-local-process race limitations remain.

## Deferred by scope or unavailable environment

The user allowed unavailable nonessential work to be skipped. The clean guest,
full manual Narrator/keyboard acceptance, trusted signing/registration and remote
CI remain explicitly unexecuted; this permission does not turn them into passes.
No signing credentials, hypervisor changes, push/tag or release publication were
used. Multi-GiB/above-4-GiB RAR and coverage-guided sanitizer fuzzing remain outside
this candidate's qualified scope. These boundaries prevent a stable-release claim.
