# Performance evidence

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
