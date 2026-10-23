# VYNX ARC 0.8.0 performance acceptance

Measured 2026-10-23 on the Windows development host: Windows 11 Pro build 26200,
Ryzen 5 7500F, 12 logical CPUs, 32 GiB RAM, NVMe/NTFS. Three trials per case;
OS filesystem cache was not flushed. RSS is a 10 ms sampled peak, not an exact
maximum. Raw samples, package hash and host snapshot are under
[benchmarks/0.8](benchmarks/0.8/).

This run was repeated after `ctutils` was updated from 0.4.2 to 0.4.5 for
`RUSTSEC-2026-0385` and the packages were rebuilt. The earlier measurements are
preserved as `*-pre-ctutils045.json`.

Artifacts measured:

- Portable package SHA-256:
  `a62df2dcf737dd484221bb875d6ef658bafc77f22c846edf7856e60897aa1d3f`.
- Core executable: `perf_milestone-0d22f5ef198702d4.exe` (release,
  SHA-256 `736ee8e768256a1a256c4d45a07d153db9eda5922196dbc1ddf10168ed570982`).
- GUI and packaged CLI: `dist/stage-160a227f8d244e12be40380b90b21caa`,
  extracted from that portable ZIP.

## Selected extraction and actual GUI

| Measurement, 100,000-entry ZIP | 0.8.0 | 0.6.0 | Change |
| --- | ---: | ---: | ---: |
| Core selected extraction median | 1.8728 s | 1.7751 s | +5.5% |
| Full GUI selected extraction median | 3.0655 s | 3.3210 s | −7.7% |
| Full GUI open median | 1.6620 s | 2.0981 s | −20.8% |
| Full GUI search p95, 60 changes | 28.5318 ms | 36.6031 ms | −22.0% |
| GUI + worker sampled RSS peak | 198.0 MiB | 200.5 MiB | −1.4% |
| GUI + worker private committed peak | 176.2 MiB | 176.3 MiB | ≈0% |
| GUI + worker handles peak | 497 | 220/211 at checkpoints | not directly comparable |

The GUI run completed 100 real open/preview/extract/close cycles. All three
resource checkpoints were captured:

| Cycle | Handles | Private MiB | RSS MiB |
| ---: | ---: | ---: | ---: |
| 10 | 317 | 14.1 | 41.7 |
| 50 | 214 | 14.4 | 34.7 |
| 100 | 214 | 16.3 | 36.1 |

Handles do not grow monotonically from cycle 50 to 100. This does not prove the
absence of a long-term leak; cycle 10 was sampled while a worker was active.

The core result still checks the complete metadata plan and decodes/verifies
unselected payloads; only destination probes for unselected entries are avoided.
The GUI result includes worker startup, protocol, metadata and UI work.

## New scenario benchmarks

Medians of three trials through the real GUI:

| Scenario | Median | Scope |
| --- | ---: | --- |
| Nested ZIP open | 0.1511 s | 501 entries in the nested archive |
| Inter-archive copy, 200 files | 9.4484 s | Real MIME paste, publication and target reload |
| Recovery plan classification | 0.1680 s | 400 entries, 200 existing outputs |
| First text preview | 0.0740 s | `PreviewDialog` |
| Cached text preview | 0.0188 s | Same preview reopened |

The copy result is the complete GUI workflow, not core copy throughput. The
recovery sample includes one 0.594 s outlier; the other two trials were 0.166 s
and 0.168 s.

## Core throughput

Seconds, medians of three trials. `many.zip` extracts one selected file; other
cases extract the complete archive.

| Corpus | Open | Test | Selected preview | Extraction | Rebuild |
| --- | ---: | ---: | ---: | ---: | ---: |
| many.zip | 1.186891 | 1.673838 | 0.921808 | 1.872757 | 9.827312 |
| large.zip | 0.000210 | 0.010530 | 0.000197 | 0.089329 | 1.038134 |
| mixed.zip | 0.013477 | 0.023700 | 0.009570 | 9.988287 | 0.214965 |
| solid.7z | 0.002208 | 0.013253 | 0.013092 | 1.559403 | 1.454198 |
| encrypted.7z | 0.010728 | 0.044773 | 0.044316 | 1.659542 | 2.915805 |
| mixed.tar.zst | 0.003729 | 0.004770 | 0.004776 | 7.579120 | — |

Compared with 0.6.0, many.zip open improved 9.6%, selected extraction regressed
5.5% and rebuild improved 2.3%. Compared with the previous 0.8.0 run,
mixed.zip complete extraction moved from 8.4605 s to 9.9883 s, while
mixed.tar.zst extraction moved from 8.0506 s to 7.5791 s. The filesystem cache
and antivirus state remain uncontrolled, so these changes are retained as
variance rather than claimed optimisation or a stable regression. Small
absolute changes for `large.zip` are measurement variation.

## Packaged CLI

Three trials per dataset/format/operation. PATH was restricted to
Windows/System32 and developer Qt variables were removed. Times include process
startup. ZIP and 7Z use application format defaults.

| Dataset | Source | ZIP create wall/CPU (s) | ZIP extract wall/CPU (s) | 7Z create wall/CPU (s) | 7Z extract wall/CPU (s) | RSS ZIP create/extract (MiB) | RSS 7Z create/extract (MiB) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1000 small text files (1 KiB) | 1000 KiB | 0.5903 / 0.5312 | 7.7995 / 5.7812 | 1.8287 / 2.2812 | 8.9632 / 7.1094 | 11.3 / 10.7 | 26.3 / 19.4 |
| 3 large random files | 48 MiB | 0.8495 / 0.7812 | 0.1656 / 0.1094 | 5.4170 / 10.6406 | 0.1243 / 0.1094 | 10.4 / 10.2 | 219.9 / 40.2 |
| Mixed 80 files | 26.25 MiB | 0.3087 / 0.2656 | 0.6919 / 0.4844 | 2.1419 / 2.1094 | 0.7729 / 0.6250 | 10.4 / 10.3 | 34.0 / 19.5 |
| Repeated text | 16 MiB | 0.0415 / 0.0312 | 0.0517 / 0.0312 | 0.2874 / 0.5000 | 0.0519 / 0.0469 | 10.2 / 10.2 | 168.6 / 26.3 |
| Deterministic random data | 16 MiB | 0.3083 / 0.2812 | 0.0727 / 0.0469 | 1.7618 / 3.3750 | 0.0616 / 0.0469 | 10.4 / 10.2 | 217.8 / 22.9 |

Real RAR4/RAR5 and multipart fixtures (small, not matched-size datasets): list
median 0.0208 s; extraction median 0.0367 s, range 0.0309–0.0935 s.

GUI startup median was 2.0220 s. A real GUI open of a 100,000-entry ZIP took
2.0577 s in one trial (134.5 MiB sampled RSS).

## Regressions and variance

These are retained as findings, not hidden behind summary numbers:

- 1000-small-file ZIP extraction is 7.7995 s versus 7.438 s in 0.6.0 (**+4.9%**).
  The previous 0.8.0 run measured 9.0119 s, and an earlier run measured
  14.5038 s. The +21% result was not reproduced. The spread is consistent with
  uncontrolled filesystem cache/antivirus state; monitoring continues, but this
  is no longer evidence of a stable 20% regression.
- 7Z creation became faster on large/random data than the historical 0.6.0 run
  (5.42 s versus 14.78 s; 1.76 s versus 4.74 s) but sampled RSS remains
  approximately doubled: 219.9 versus 102.7 MiB and 217.8 versus 102.6 MiB.
  The cause is not localised; this is an open performance defect.
- Core selected extraction remains 5.5% slower than 0.6.0 (1.8728 s versus
  1.7751 s), while the full GUI path is 7.7% faster. `core/src` is unchanged in
  the 0.8.0 working tree, so this is inherited from the 0.7.0/0.6.0 baseline.
- The previous packaged CLI run is preserved as
  [RC_PERFORMANCE-pre-ctutils045-run2.json](benchmarks/0.8/RC_PERFORMANCE-pre-ctutils045-run2.json);
  the current run is [RC_PERFORMANCE.json](benchmarks/0.8/RC_PERFORMANCE.json).
- No WinRAR/7-Zip comparison, clean VM, disk-cache control, long-run leak test or
  million-entry memory measurement was performed.

## Reproduce

```powershell
python scripts/benchmark-milestone.py --fixtures .dev/bench-0.8/fixtures --prepare
python scripts/benchmark-milestone.py --fixtures .dev/bench-0.8/fixtures `
  --test-exe target/release/deps/perf_milestone-<hash>.exe --output .dev/bench-0.8/core
python scripts/benchmark-gui.py --app dist/stage-<id> `
  --archive .dev/bench-0.8/fixtures/many.zip --output .dev/bench-0.8/gui
$env:VYNX_BENCH_STAGE = (Resolve-Path dist/stage-<id>).Path
python scripts/benchmark-rc.py --trials 3
```

The GUI script sets `QT_QPA_PLATFORM=offscreen`; the packaged stage must contain
`platforms/qoffscreen.dll` for this measurement only. That plugin is not part of
the portable package.


## Follow-up 2026-10-23

See [ACCEPTANCE_0_8_FOLLOWUP.md](ACCEPTANCE_0_8_FOLLOWUP.md) for fresh source
gates (95 Rust tests, CTest 5/5), user-confirmed Narrator smoke, agent language
review, the accepted workload-specific 7Z memory tradeoff and the diagnosed
clean-Windows blocker (Sandbox 0x80070103, hypervisor not running). This
follow-up supersedes earlier open-memory/basic-Narrator status, but does not
claim full manual workflow, human language-matrix or clean-VM acceptance.
