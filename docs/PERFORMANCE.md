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
without creating a widget per row. It is a correctness test, not a timed benchmark.

Large-archive/million-entry memory, cold-start latency, codec throughput, long
operations, and SSD/HDD comparisons remain unmeasured. The synthetic progress
screenshot is presentation QA, not a throughput sample. These results do not
establish the master brief's broader performance acceptance targets.
