# VYNX ARC 0.6 performance acceptance

Measurements on the existing Windows development host, with the same six 0.5
corpora. Three trials per throughput case; OS filesystem caches were not flushed.
The first GUI operation is process-first, not a claim of a cold filesystem cache.
Benchmarks ran after the mutation campaigns and native builds finished.

## Selected extraction and actual GUI

| Measurement, 100,000-entry ZIP | Result |
| --- | ---: |
| Core selected extraction, 0.5 saved baseline | 40.140660 s |
| Core selected extraction, current median | 1.775075 s |
| Full GUI selected extraction, current median | 3.321038 s |
| Full GUI open including model, current median | 2.098131 s |
| Full GUI search p95, 60 query changes | 36.6031 ms |
| Actual large-ZIP test cancellation through GUI | 1.008773 s |

The core path is approximately 22.6 times faster for this selection. It still
checks the complete metadata plan and decodes/verifies unselected payloads; the
optimization removes destination filesystem probes for unselected entries.
The GUI result includes worker startup, protocol, metadata and UI work. It meets
the under-five-second target independently of the core-only comparison.

Two complete GUI runs passed, each with three large-archive trials and 100 real
open/preview/extract/close cycles. The second run additionally cancels an actual
decoder operation. Combined GUI/live-worker sampled peaks: 200,822,784 RSS bytes
and 176,320,512 private committed bytes, at most two child processes. RSS sums
shared pages and is not physical memory used exclusively by this application.
At cycle 10/100 checkpoints, handles were 220/211 and private bytes
15,323,136/12,083,200. No retention growth was observed over this run. Checkpoints
are asynchronous external samples; the cycle-50 sample overlaps the next worker.
These measurements do not prove absence of all long-term leaks.

## Core throughput

| Corpus | Open | Test | Selected preview | Extraction | Rebuild |
| --- | ---: | ---: | ---: | ---: | ---: |
| many.zip | 1.312318 | 1.743516 | 0.943330 | 1.775075 | 10.059775 |
| large.zip | 0.000262 | 0.008828 | 0.000273 | 0.098057 | 1.127338 |
| mixed.zip | 0.013464 | 0.022370 | 0.009704 | 8.080356 | 0.237945 |
| solid.7z | 0.002294 | 0.014710 | 0.014351 | 1.653261 | 1.605888 |
| encrypted.7z | 0.010557 | 0.047543 | 0.046463 | 1.775673 | 3.099167 |
| mixed.tar.zst | 0.003653 | 0.004955 | 0.004551 | 8.050844 | — |

Seconds, medians of three trials. Extraction selects one file only for many.zip;
other rows extract the complete archive, matching the 0.5 benchmark definition.
Solid 7Z may decode related blocks and has no promised random-access speedup.

Relative to saved 0.5, many.zip open is 7.5% slower and preview 2.3% slower;
encrypted.7z rebuild is 8.6% slower. No nontrivial operation regressed by more than
10% in these samples. large.zip open/preview changed by +12.6%/+44.9%, but the
absolute changes are about 0.029/0.084 milliseconds; these very short timings are
retained as measurement variation, not hidden. Faster full encrypted extraction
does not imply a new decoder optimization; filesystem and scheduling variation
remain uncontrolled across historical runs.

Raw reports include trials, corpus/executable identities, cancellation, handles
and working-set samples: [core](benchmarks/0.6/core-current.json),
[GUI](benchmarks/0.6/gui-current.json), [earlier GUI](benchmarks/0.6/gui-first.json).
Reproduce with `scripts/benchmark-milestone.py`, `scripts/benchmark-gui.py` and
the opt-in Rust benchmark fixture runner. Final package smoke and upgrade evidence
are reported separately; these are development acceptance measurements.
