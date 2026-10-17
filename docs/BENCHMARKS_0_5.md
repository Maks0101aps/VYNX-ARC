# 0.5 performance evidence

Same Windows host, release profile and exact six corpus files for both versions.
Baseline: source tag `v0.4.0`; current: the 0.5 core/model implementation. Benchmark
instrumentation was added to an isolated baseline source copy without product changes.
See [host](benchmarks/0.5/host.json), [fixtures](benchmarks/0.5/fixtures.json),
[baseline](benchmarks/0.5/core-baseline.json), [current](benchmarks/0.5/core-current.json).

## Core medians

Seconds, **0.4 / 0.5**, three trials per cell. Extraction selects one file for the
100k ZIP and the whole archive for other cases. Modification rebuilds a copied
ZIP/7Z while renaming one entry. Filesystem/AV/cache effects are part of these wall
clock measurements; these are not promises for other machines.

| Corpus | Open | Test | Selected preview | Extract | Modify |
| --- | --- | --- | --- | --- | --- |
| many.zip | 1.33977 / 1.22079 | 1.65745 / 1.69818 | 1.63943 / 0.92224 | 39.86287 / 40.14066 | 9.94424 / 10.21002 |
| large.zip | 0.00027 / 0.00023 | 0.01171 / 0.01068 | 0.00975 / 0.00019 | 0.09909 / 0.09548 | 1.09176 / 1.07202 |
| mixed.zip | 0.01392 / 0.01307 | 0.02220 / 0.02330 | 0.02232 / 0.00956 | 7.93652 / 8.19205 | 0.22466 / 0.23987 |
| solid.7z | 0.00228 / 0.00281 | 0.01365 / 0.01432 | 0.01338 / 0.01409 | 2.88637 / 1.55521 | 1.60073 / 1.55690 |
| encrypted.7z | 0.01039 / 0.01021 | 0.04447 / 0.04367 | 0.04405 / 0.04430 | 2.92355 / 2.95332 | 2.94702 / 2.85395 |
| mixed.tar.zst | 0.00382 / 0.00465 | 0.00494 / 0.00641 | 0.00500 / 0.00538 | 8.56147 / 8.17608 | - |

100k selected extraction remains about 40 seconds: its complete preflight is still
expensive. ZIP preview checks the entire metadata policy and reparses the directory,
but only decodes the requested payload. Thus 100k preview still costs about 0.92 s;
the small-file selection inside the large ZIP improves from 9.75 to 0.19 ms.
Solid 7Z still decodes a related block; no random-access claim is made for it.

## GUI model

Synthetic 100000-entry nested model, visible 1060x720 table, name sorting, 3 warmup
and 20 measured query cycles. The operation includes switching to archive-wide
search, filtering, sorting and event processing. Raw query samples are retained in
[0.4](benchmarks/0.5/ui-baseline.json) and [0.5](benchmarks/0.5/ui-current.json).

| Metric | 0.4 | 0.5 |
| --- | --- | --- |
| Query median | 259.33 ms | 56.02 ms |
| Query p95 | 289.89 ms | 62.70 ms |
| Model preparation | 18.64 ms | 84.29 ms |
| Sampled GUI process peak RSS | 96.41 MiB | 95.20 MiB |

Preparation costs more because it constructs cached rows and locale sort keys.
It now runs on the archive worker before adoption by the GUI. This is an accepted
tradeoff for faster queries, not an opening-speed improvement. The GUI memory/CPU
measurement is a separate retained run; it does not replace the primary latency
samples. See [resources](benchmarks/0.5/ui-resources.json).

## Resource and cancellation checks

Core process RSS was sampled every 10 ms (not an exact peak). Largest sampled RSS:
about 165 MiB on 100k ZIP. The largest relative RSS increase across the six cases is
6.1%, below the 15% investigation threshold. GUI-model RSS is recorded separately
and must not be confused with full archive-window memory on every possible corpus.

Every case also ran 10 warmup and 100 open/preview/close cycles on one fixed small
ZIP. Handles were flat in all six runs, with no positive before/after delta. Working
sets stabilized after warmup; raw samples remain in the reports. These retention
cycles do not qualify repeated full extraction of every large corpus. Separate
publication stress covers 4900 real ZIP publications with and without Zone.Identifier.

All six cancellation probes returned errors after cancellation; measured latency was
0.10-8.68 ms. These short fixtures do not establish a universal decoder latency bound.

## Investigated regressions and scope

- solid.7z open: 0.0022770 -> 0.0028130 s (+23.5%, absolute +0.536 ms)
- mixed.tar.zst open: 0.0038168 -> 0.0046459 s (+21.7%, absolute +0.829 ms)
- mixed.tar.zst test: 0.0049424 -> 0.0064107 s (+29.7%, absolute +1.468 ms)

The 7Z opening implementation is unchanged: the archive-source diff adds the ZIP
selected-member path. The observed solid-open difference is 0.536 ms over only
three trials, so it is retained as a small unresolved timing variation rather than
hidden by selecting another run. No throughput cell regressed by more than 10% on
a multi-second operation. GUI preparation increased by design as described above.

The initial resource-cycle calibration was stopped because repeating a 100k corpus
100 times mixed retention checking with expensive throughput. The retained
[calibration record](benchmarks/0.5/calibration.json) explains the scope change.
Both final versions use the same fixed small retention corpus and the same six
three-trial throughput datasets. Seeded payloads are reproducible; generated archive
timestamps can vary, so regenerated containers need their own hash manifest.
The exact corpus files used for this comparison are hashed in the retained manifest.
