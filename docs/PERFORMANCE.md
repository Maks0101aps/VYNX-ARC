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
without creating a widget per row. A separate real GUI capture opened a ZIP with
100,000 entries in 2.014 seconds end-to-capture, used 1.453 CPU seconds and had a
119.4 MiB observed RSS peak. Repeated close/reopen memory retention was not measured.

## Packaged operation measurements

Single trial 2026-10-02 on Windows 11 Pro build 26200, Ryzen 5 7500F, 12 logical
CPUs, 32 GiB RAM and fixed NVMe/NTFS. The release CLI ran with PATH limited to
Windows/System32 and developer Qt variables removed. ZIP/7Z use actual format
defaults. Wall and CPU times include process startup. Peak RSS is observed at 10ms
polling and is not an exact maximum. Extracted bytes were compared with inputs.

| Dataset | Source | ZIP create wall/CPU (s) | ZIP extract wall/CPU (s) | ZIP bytes | 7Z create wall/CPU (s) | 7Z extract wall/CPU (s) | 7Z bytes | Peak RSS ZIP/7Z (MiB) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1000 small text files (1 KiB each) | 1,000 KiB | 0.557 / 0.500 | 8.271 / 5.203 | 155,120 | 1.899 / 2.203 | 8.110 / 6.141 | 48,516 | 11.2 / 25.5 |
| 3 large random files | 48 MiB | 0.877 / 0.797 | 0.236 / 0.188 | 50,347,495 | 12.818 / 12.688 | 0.145 / 0.109 | 50,334,257 | 10.0 / 102.7 |
| Mixed 80 files | 26.25 MiB | 0.289 / 0.234 | 0.612 / 0.391 | 13,207,232 | 2.662 / 2.641 | 0.661 / 0.469 | 13,115,386 | 10.1 / 31.9 |
| Repeated text | 16 MiB | 0.053 / 0.047 | 0.052 / 0.000 | 95,439 | 0.754 / 0.750 | 0.063 / 0.047 | 2,751 | 10.2 / 28.0 |
| Deterministic random data | 16 MiB | 0.311 / 0.266 | 0.083 / 0.063 | 16,782,611 | 4.311 / 4.219 | 0.062 / 0.063 | 16,778,206 | 10.0 / 102.6 |

Three fresh Home launches through completed QWidget capture took 1.983, 1.991 and
2.003 seconds; observed RSS peaks were about 45 MiB. The 1.8-second capture timer
is included, so these are not time-to-first-paint measurements.

Four small real RAR4/RAR5 and multipart fixtures completed packaged list/extract
in 0.021вЂ“0.083 seconds. They are too small for meaningful throughput comparison.
Password-protected RAR correctness is covered by native tests; the CLI has no
password argument.

`RC_PERFORMANCE.json` stores each raw sample and is ignored as generated output.
Run `python scripts/benchmark-rc.py` to recreate the deterministic matrix. Each operation
was measured once, without cache control or statistical spread; short CPU samples
quantize at Windows timer resolution. No 7-Zip/WinRAR comparison was performed.
Million-entry memory, repeated archive open/close leak growth, long operations,
device comparisons and broad performance targets remain unverified.
