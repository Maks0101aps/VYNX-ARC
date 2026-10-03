# Performance evidence

## Post-0.2 development matrix — 2026-10-03

Development branch `feature/0.3-development`, Windows 11 Pro x64 build 26200,
Ryzen 5 7500F (12 logical CPUs), approximately 32 GiB RAM. The optimized MSVC
CLI uses the new settings; it is a development build, not an accepted release.
Public version remains 0.1.0 and the 0.2.0 publication gate remains blocked.

The final matrix measured ZIP and 7Z creation/extraction for four presets
(Store/Fast/Balanced/Maximum), three resource modes (Eco/Balanced/Maximum) and
four datasets: 32 MiB deterministic random data, 32 MiB repeated text, 1,000
small text files, and a mixed tree (16 MiB random, about 16 MiB repeated data,
128 small files). All **192 process measurements / 96 archive pairs** completed
with independent SHA-256 verification of every extracted file. Each configuration
has one observation; these are not medians or statistically controlled speed claims.
An earlier 192-measurement matrix also passed, using a different intermediate CLI;
the tables and raw files below use only the final build.

Wall time includes process startup, creation verification and 10 ms polling.
CPU time sums native user/kernel process times. Peak RSS uses Windows
PeakWorkingSetSize sampled during process lifetime, so an unsampled final peak
may be missed. Output size is measured archive bytes; throughput divides source
bytes by wall time. Cache state, Defender/indexing and other host activity were
uncontrolled. Some GUI validation overlapped this run. No environment changes or
competitor benchmarks were performed. These measurements do not identify or
resolve the separate project-location ZIP replacement blocker.

CLI SHA-256: `bdb9bef60019a6ed61c2efddf4ec59604cd75959d9d696c7c7ae993c9621da2e`.
Archive workspace: `C:\Users\Maksi\AppData\Local\Temp\vynx-dev-benchmark-zk2dna_w`.
Full per-preset/per-resource wall, CPU, RSS, output-byte and throughput records
are in [development-20261003.csv](benchmarks/development-20261003.csv); machine,
binary, source and dataset hashes are in
[development-20261003.json](benchmarks/development-20261003.json).
The measurement HEAD is the base commit with feature changes then uncommitted;
source fingerprints and the executable hash identify the measured implementation.
Reproduce with `python scripts/benchmark-development.py --mib 32` after a release
build. Generated reports retain their own artifact identities in `.dev`.

### Balanced preset / Balanced resource observations

| Dataset / format | Source MiB | Create wall / CPU (s) | Extract wall / CPU (s) | Archive bytes | Create / extract peak RSS (MiB) | Create / extract MiB/s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| random / ZIP | 32.00 | 0.550 / 0.531 | 0.098 / 0.062 | 33,564,897 | 10.0 / 9.8 | 58.19 / 325.86 |
| random / 7Z | 32.00 | 4.086 / 8.031 | 0.088 / 0.094 | 33,556,200 | 240.1 / 50.0 | 7.83 / 362.70 |
| compressible / ZIP | 32.00 | 0.067 / 0.031 | 0.056 / 0.047 | 173,051 | 10.6 / 9.8 | 474.40 / 567.00 |
| compressible / 7Z | 32.00 | 0.566 / 1.125 | 0.077 / 0.062 | 5,478 | 184.0 / 45.5 | 56.56 / 415.67 |
| small-files / ZIP | 2.08 | 0.566 / 0.516 | 7.109 / 4.734 | 153,782 | 11.0 / 10.7 | 3.67 / 0.29 |
| small-files / 7Z | 2.08 | 1.897 / 2.219 | 8.031 / 6.500 | 49,363 | 27.5 / 19.1 | 1.10 / 0.26 |
| mixed / ZIP | 32.20 | 0.408 / 0.344 | 0.937 / 0.609 | 16,882,512 | 10.2 / 9.9 | 78.93 / 34.37 |
| mixed / 7Z | 32.20 | 2.697 / 5.016 | 1.079 / 0.766 | 16,787,537 | 227.3 / 46.6 | 11.94 / 29.84 |

### Ranges across all twelve preset/resource configurations

| Dataset / format | Create wall min–max (s) | Extract wall min–max (s) | Highest observed RSS (MiB) |
| --- | ---: | ---: | ---: |
| random / ZIP | 0.108–0.628 | 0.068–0.181 | 10.6 |
| random / 7Z | 0.140–9.888 | 0.067–0.100 | 455.8 |
| compressible / ZIP | 0.067–0.120 | 0.056–0.078 | 10.6 |
| compressible / 7Z | 0.109–1.609 | 0.067–0.100 | 330.3 |
| small-files / ZIP | 0.456–0.601 | 6.709–7.938 | 11.2 |
| small-files / 7Z | 0.545–4.605 | 6.951–11.744 | 49.5 |
| mixed / ZIP | 0.160–0.443 | 0.879–1.093 | 10.7 |
| mixed / 7Z | 0.224–5.574 | 0.900–1.862 | 265.4 |

ZIP encoding remains single-threaded in every resource mode. 7Z thread/dictionary
choices materially change CPU and memory cost; Maximum is not guaranteed faster
or smaller for every dataset. Resource budgets are encoder planning estimates,
not hard RSS ceilings. New-format performance, battery/energy use, long-term
memory retention and other hardware remain unmeasured. See DEVELOPMENT.md for
actual settings and limitations.

## Historical 2026-10-02 milestone evidence

Measured 2026-10-02 on Windows 11 x64 build 26200, Ryzen 5 7500F, approximately
32 GiB RAM. Qt 6.12.0 MSVC and an optimized Rust core are used in both revisions.
Raw samples and executable SHA-256 values are in UI_MEMORY.json.
This generated report is local and ignored by Git; reproduce it with the RAM
runner documented in UI_QA.md. The table below preserves the recorded results.

## Settled working set before/after the UI refactor

Same small ZIP, English, 100% scaling, no recent history, isolated developer PATH.
Three fresh launches per state/theme/revision; sample after five seconds settling
and one second CPU observation. These are median RSS values, in MiB:

| State | Theme | Before | After | Delta |
| --- | --- | ---: | ---: | ---: |
| Home | Light | 40.00 | 40.53 | +0.53 |
| Home | Dark | 39.98 | 40.25 | +0.28 |
| Archive | Light | 40.89 | 40.87 | -0.02 |
| Archive | Dark | 40.84 | 40.71 | -0.13 |

No material browser RAM increase was observed; the small decrease is insufficient
to claim an optimization. Home increased by under 0.6 MiB. All 24 one-second idle
CPU samples reported 0.0%; this is sampling resolution, not a guarantee of zero
activity. The largest after-refactor startup RSS observed by 100ms polling was
40.98 MiB. It is not an exact peak or memory ceiling.

Before uses the preserved baseline deployment; after uses the final UI/test
deployment. Scripts restore configuration and terminate the read-only sampled
processes. Screenshot samples.json files retain separate one-second launch
measurements; those must not be confused with the settled medians above.

The real GUI smoke test exercises 100,000 metadata rows through Qt model/view,
without creating a widget per row. Repeated close/reopen memory retention was not measured.

## Packaged operation measurements

Three trials per CLI dataset/format/operation, measured 2026-10-02 on Windows 11
Pro build 26200, Ryzen 5 7500F, 12 logical CPUs, 32 GiB RAM and fixed NVMe/NTFS.
The release CLI ran with PATH limited to Windows/System32 and developer Qt variables
removed. ZIP/7Z use actual format defaults. Table values are medians; each extraction
was byte-compared to its source. Wall and CPU times include process startup. Peak RSS
is the median of three observed 10ms-polling peaks, not an exact maximum. OS cache
state was uncontrolled.

| Dataset | Source | ZIP create wall/CPU (s) | ZIP extract wall/CPU (s) | ZIP bytes | 7Z create wall/CPU (s) | 7Z extract wall/CPU (s) | 7Z bytes | Peak RSS ZIP/7Z (MiB) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1000 small text files (1 KiB each) | 1000 KiB | 0.559 / 0.531 | 7.438 / 4.859 | 155,120 | 1.963 / 2.172 | 9.652 / 6.328 | 49,272 | 11.0 / 25.9 |
| 3 large random files | 48 MiB | 0.919 / 0.812 | 0.208 / 0.109 | 50,347,495 | 14.781 / 14.156 | 0.168 / 0.141 | 50,334,258 | 10.0 / 102.7 |
| Mixed 80 files | 26.25 MiB | 0.321 / 0.281 | 0.652 / 0.391 | 13,207,232 | 3.099 / 3.000 | 0.819 / 0.531 | 13,115,464 | 10.1 / 31.8 |
| Repeated text | 16 MiB | 0.052 / 0.031 | 0.082 / 0.031 | 95,439 | 0.938 / 0.859 | 0.083 / 0.047 | 2,753 | 10.0 / 86.5 |
| Deterministic random data | 16 MiB | 0.344 / 0.297 | 0.082 / 0.047 | 16,782,611 | 4.743 / 4.625 | 0.062 / 0.031 | 16,778,206 | 10.1 / 102.6 |

Median source throughput in MiB/s, including process startup:

| Dataset | ZIP create | ZIP extract | 7Z create | 7Z extract |
| --- | ---: | ---: | ---: | ---: |
| 1000 small files | 1.75 | 0.13 | 0.50 | 0.10 |
| 3 large random files | 52.25 | 230.55 | 3.25 | 286.57 |
| Mixed 80 files | 81.75 | 40.28 | 8.47 | 32.05 |
| Repeated text | 307.69 | 194.41 | 17.05 | 192.31 |
| Deterministic random data | 46.57 | 194.41 | 3.37 | 257.24 |

Three fresh Home launches through completed QWidget capture took 2.005, 1.977 and
1.992 seconds; observed RSS peaks were about 45 MiB. The 1.8-second capture timer
is included, so these are not time-to-first-paint measurements. A real GUI capture
opened a ZIP with 100,000 entries in 2.017 seconds end-to-capture, used 1.484 CPU
seconds and had a 119.3 MiB observed RSS peak. This 100k test was one trial.

Four small real RAR4/RAR5 and multipart fixtures each completed packaged list and
extract in three trials. Median list took about 0.021 seconds; median extraction
ranged from 0.031 to 0.085 seconds. They are too small for meaningful throughput
comparison.
Password-protected RAR correctness is covered by native tests; the CLI has no
password argument.

`RC_PERFORMANCE.json` stores each raw sample and is ignored as generated output.
Run `python scripts/benchmark-rc.py` to recreate the deterministic matrix; pass
`--trials N` to change the default three repetitions. Short CPU samples quantize
at Windows timer resolution. No 7-Zip/WinRAR comparison was performed.
Million-entry memory, repeated archive open/close leak growth, long operations,
device comparisons and broad performance targets remain unverified.
