# 0.9.0 acceptance - development candidate, blocked

Date: 2026-10-24. Base: `release/0.8.0` / `1225285`.
Work branch: `development/0.9.0`; implementation and test changes are committed
and pushed. Exact qualified source/test commit: `ed65934`. No tag or release
publication was performed. This remains a development candidate.

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

## Qualification status and remaining distribution boundaries

| Area | Current result |
| --- | --- |
| RAR4/RAR5 read, list, test, extract | Implemented; independent native reader retained |
| Native RAR5 creation | Passed real presets, solid, dictionary, AES/header encryption, volumes and recovery checks; 21/21 WinRAR cases |
| RAR5 add/delete/rename/save-back | Verified single-volume unlocked/unsigned rebuilds; real keyboard rename independently decoded by WinRAR |
| Keyboard and Narrator | Passed scoped interactive RAR workflow; [observations](verification/0.9/keyboard-narrator.json) and speech-recap screenshots retained |
| Host installation without Sandbox | Portable, per-user upgrade/uninstall and preserved settings/working copies passed; separate test-signed Setup upgrade also passed |
| Windows GitHub Actions | Passed on [`ed65934`](https://github.com/Maks0101aps/VYNX-ARC/commit/ed65934a100a4783562d17e6e469d24dda226fbd): [run40082225124](https://github.com/Maks0101aps/VYNX-ARC/actions/runs/40082225124), source110, CTest5/5, nativeRAR21 and1,000-file round-trip |
| Test signing | Signed Portable, Setup and sparse MSIX prepared and checked; local nonexportable certificate, deliberately untrusted |
| Public signing / trusted Explorer registration | Not performed; no public signing identity supplied; user selected TESTONLY signing |
| Clean interactive Windows guest | Unavailable (`HypervisorPresent=False`); host acceptance is explicitly substituted, not relabeled clean-guest acceptance |

The current native ReplaceFileW path passed two full matrices (2,800/2,800),
compatible-reader controls and safe locked-target refusal. Same-volume recovery
backups and zero metadata-ignore flags protect partial native failures. Errors
1176/1177 are tested with deterministic models, not kernel fault injection.
Earlier MoveFileExW error-5 failures on candidate and 0.8.0 remain recorded;
exact attribution of every historical external failure remains unknown.

Keyboard acceptance used the deployed GUI with real Narrator Speech recap:
RAR5/Maximum, password, solid, encrypted filenames, recovery3%, native file picker,
creation, wrong-password retry, opening, search, F2 rename, Ctrl+E extraction,
custom-volume controls, validation dialog and Escape cancel. One focus-recovery
mouse click was used; this is not claimed as an uninterrupted mouse-free session.
The source and extracted bytes match, including independent WinRAR extraction
before/after GUI rename. Automatic full validation-text narration was not confirmed;
text is exposed in the accessibility tree and dialog/OK were spoken. Historical
complete nested/copy/recovery manual coverage remains outside this scoped pass;
automated workflows are separate. No all-language/accessibility certification.
Narrator and the owned test app were stopped at completion.

A separate TESTONLY certificate/signature is not public Windows trust. No Root or
TrustedPeople import, Developer Mode/security change, or SmartScreen bypass was
used. [Signing](SIGNING.md) records the certificate, expiry, tamper test and public
signing route. The MSIX remains an Explorer sparse identity, not a full installer.
No tag/release publication or broad stable-release assertion was made.

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

The user allowed unavailable nonessential work to be skipped and explicitly
requested host checks without Sandbox, real keyboard/Narrator work, push/Actions
and TESTONLY signing. The scoped checks above fulfill that follow-up. A clean
interactive guest, publicly trusted certificate, trusted Explorer registration,
extended manual nested/copy/recovery scenario, multi-GiB RAR and coverage-guided
sanitizer fuzzing remain outside the qualified scope; none is recorded as passed.

## Hosted independent-oracle correction

The first pushed run40080343410 passed source/package gates but its System32
bsdtar could not represent the required Cyrillic pathname under Windows1252.
A controlled same-decoder en-US process manifest reproduced the failure, and an
UTF-8 process manifest passed exact names/hashes. The test harness now prepares
only an isolated test oracle copy using the existing Windows SDK; every non-resource
PE section must match the untouched system decoder. No shipped binary or Windows
configuration is changed. The modified copy is explicitly not claimed Microsoft-signed.
[Diagnostic evidence](verification/0.9/ci-oracle-diagnostic.json) preserves controls.
This bsdtar accepts one deliberately CRC-corrupt stored payload with exit0: the
independent decoded-tree hash gate rejects it, as do native test and WinRAR. Header
CRC refusal remains mandatory. No decoding failure or Unicode assertion is ignored.
