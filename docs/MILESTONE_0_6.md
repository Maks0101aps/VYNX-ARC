# VYNX ARC 0.6.0 — acceptance report

Release date: **2026-10-20**. Base: `v0.5.0` / `be1befc`.
Packaged application sources: **`bedc03bc947b74d60ba0fbc9c229b1456cbf3c8d`**.
Subsequent report changes do not alter compiled application inputs. Local release
acceptance passed; remote CI/publication state and exact artefact identities are
recorded in [VERIFICATION_0_6.json](VERIFICATION_0_6.json).
The [remote Windows build](https://github.com/Maks0101aps/VYNX-ARC/actions/runs/39386567084)
passed source gates, native tests, packaging and deployed verification for that
application-source commit.

This is a completed development milestone, not acceptance of the entire V1 brief.
The [development log](DEVELOPMENT_0_6.md) retains earlier failures and corrections.
This report defines the current acceptance boundary.

## Delivered behavior

1. **Large archives:** selected extraction validates the full plan and payloads
   without probing destination paths for unselected files. Session metadata serves
   navigation/search; worker reopening rejects stale entry identities.
2. **Bounded workers:** GUI archive reads/writes use owned child processes with
   bounded messages, two-worker admission, Windows Job Objects, memory profiles,
   time limits and forced cancellation. Other tabs survive worker failures.
3. **Recoverable queue:** complete large selections use verified sidecars and a
   migrated journal. Publication records expected before/after identities before
   acknowledgement. Unknown results require explicit read-only verification.
   Failed-only batch retries retain original errors and attempt relationships.
4. **Conversion:** supported RAR/TAR/ZIP/7Z/stream inputs become new verified ZIP/7Z
   archives. The dialog shows a plan and compression/resource/encryption choices.
   Source/destination passwords are separate; decrypted output requires consent.
5. **Editing:** a dock exposes working-copy details, verification and Save as new
   archive. Unknown-save recovery uses a recorded input digest; newer working-file
   bytes remain dirty. Admission is bounded without deleting existing edits.
6. **Interface/delivery:** queue filters/counters, visible narrow-window action bars,
   RU/UK translations, scrollable home content, packaged worker, actual installer
   upgrade and repaired Open With registration.

Native execution is typed and centrally supervised. Legacy GUI adapters still use
closures and JSON descriptions; not every GUI dispatch type has been replaced.

## Performance

The saved 0.5 baseline and new measurements use the same Ryzen 5 7500F / 12 logical
processors / approximately 32 GiB / Windows 11 build 26200 host and corpus identities.

| Measurement | Result |
| --- | ---: |
| Core selected extraction, 100k ZIP: 0.5 → 0.6 | 40.140660 → 1.775075 s |
| Full GUI selected extraction, three-trial median | 3.321038 s |
| Full GUI open/model, three-trial median | 2.098131 s |
| Full GUI search p95 | 36.6031 ms |
| Actual large-ZIP test cancellation through GUI | 1.008773 s |

Two GUI runs each included 100 real open/preview/extract/close cycles. Normal
GUI/live-worker sampled peak RSS was about 201 MB; cycle-10/100 private bytes and
handle counts did not show retention growth. Six core corpora changed peak RSS by
-3.7% to +2.3% versus saved 0.5. Initial memory profiles are conservative operational
caps validated against these corpora/control tests, not universal calibration.

OS caches were not flushed: process-first/repeated samples are retained, but this
is not a cold-filesystem-cache experiment. Tiny timing variation and regressions
are disclosed in [BENCHMARKS_0_6.md](BENCHMARKS_0_6.md).

## Verification

| Check | Result / evidence |
| --- | --- |
| Source gates | fmt, Clippy with warnings denied, locked Rust workspace tests and audit including yanked rejection passed |
| Native release | presentation, shell_smoke, gui_smoke and gui_batch passed |
| Worker protocol | Eight integration tests: frames, encryption, cancellation, stale metadata, publication decisions and conversion |
| GUI faults/recovery | Seven scenarios: crash, hang, allocation refusal, before/after publication, newer edit retained, mixed batch/failed-only retry; [report](verification/0.6/worker-recovery-final.json) |
| Production hooks | Normal packaged worker ignored diagnostic fault environment; hooks excluded from default compilation |
| Independent readers | 44 plain/encrypted ZIP/7Z conversion results, exact hashes/directories; [report](verification/0.6/conversion-independent-final.json) |
| Actual editors | VS Code 1.142.0 and Writer 26.8.1.1 each saved four plain/encrypted ZIP/7Z copies across client restarts; [report](verification/0.6/real-editors-final.json) |
| C++ ASan | Four final CTests; earlier candidate also passed large-archive/100-cycle GUI workflow without a finding; [scope](verification/0.6/asan-final.json) |
| Mutation campaigns | 61,321 cases / 15,499.313 aggregate active wall seconds across four workers; [report](verification/0.6/fuzz-campaign.json) |
| Final CLI parser smoke | 512/512 mutations; [report](verification/0.6/parser-smoke-final.json) |
| ZIP publication | 4,900/4,900 attempts including Mark-of-the-Web cases; [report](verification/0.6/publication-final.json) |
| Locks / interruption | Six lock/preservation/retry cases and two forced rebuild interruptions; [locks](verification/0.6/locks-final.json), [interruption](verification/0.6/interrupted-final.json) |
| Deployed portable | SDK paths removed, real GUI batch/retry, independent readers, no remaining owned app; [report](verification/0.6/portable-final.json) |
| Installer 0.5 → 0.6 | Installed GUI/CLI, preserved settings/schema-1 working-copy fixture, nine Open With mappings, unchanged defaults, uninstall; [report](verification/0.6/installer-upgrade-final.json) |
| UI | 96 deployed EN/UK/RU light/dark captures at Qt 100/125/150/200%, plus 12 narrow captures; [manifest](verification/0.6/ui-final.json) |

The first mutation campaign retained two seven-second harness timing alarms. Both
inputs passed 16 bounded replays each; their original cause remains unestablished.
The continuation passed. Both alarms are retained, rather than labelling every run
passed. Aggregate wall time includes startup; it is not CPU time, coverage-guided
fuzzing or certification. No reproducible native parser crash was found. There is
no minimization claim for non-reproducible timing alarms.

C++ ASan does not instrument Rust, decoder libraries or prebuilt Qt. STL container
annotations are disabled consistently with the static libraries. It is not leak
detection; instrumented memory/timings do not qualify release performance.
Independent-reader expected source hashes originate from core; result decoding is
independent. Editor tests use actual APIs/private profiles, not manual window tests.
UI contact sheets were visually reviewed, with full-size RU/UK and narrow samples.

Publication/lock/parser reports identify the CLI hash unchanged in the final
package. Portable and upgrade checks ran again on the final GUI/installer package.

## Explicit boundaries

- Worker caps retain the user's filesystem authority, exclude GUI/editor memory
  and are not a complete allocation audit. Bounded Qt raster decoding remains in
  GUI. Legacy CLI commands are direct; isolated alternatives are explicit.
- History disabled means session-only queue recovery. Interrupted extraction has
  no aggregate receipt and requires destination review. Unknown writes never retry
  automatically. Physical power-loss durability is untested.
- Working fingerprints are hints; saves use hashes. The 2 GiB / 32-copy limit is
  admission control; external growth and plaintext copies are retained until disposal.
- Conversion keeps names, contents, directories and representable modified times;
  not comments, ACLs, ownership or extra metadata. ZIP precision is two seconds.
  RAR creation, auto-merge and transfers between archives remain outside scope.
- Packages are unsigned. Trusted Explorer identity installation, SmartScreen,
  enterprise policy, independent clean-machine qualification and physical OS DPI/
  monitor transitions remain unqualified. Shell COM smoke and Open With installation
  are qualified separately. Hyper-V is not a release gate.
- Unicode normalization equivalence and full hostile-local-process filesystem race
  hardening remain in the [security model](SECURITY_MODEL.md).

Further work: coverage-guided/parser sanitizer testing, automatic extraction
reconciliation, remaining GUI dispatch types and signed distribution.
