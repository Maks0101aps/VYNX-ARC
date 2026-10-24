# 0.9.0 measurements - 2026-10-24

These are measured corpus results, not throughput guarantees or stable-release acceptance.
Exact binaries and raw samples: [verification/0.9](verification/0.9).
Windows x64 development host; fixed corpus seeds; 10 ms process-tree RSS sampling for RAR.
RAM samples can miss peaks and exclude unrelated processes. They are not committed-memory limits.

## Native RAR5 matrix

Mixed corpus: 1,084,288 bytes in 3 files, plus directories; Unicode and an empty file/directory.
Every row used actual native compression and independent WinRAR 7.22 test/extract with SHA-256 equality.
One trial per matrix row. Close timings and identical compressed sizes do not rank compression presets.

| Resource | Preset | Create incl. verify (s) | Sampled tree RSS (MiB) | Archive bytes |
| --- | --- | ---: | ---: | ---: |
| eco | store | 0.286 | 32.6 | 1,084,818 |
| eco | fast | 0.315 | 31.2 | 524,890 |
| eco | balanced | 0.291 | 33.7 | 524,890 |
| eco | maximum | 0.178 | 33.2 | 524,890 |
| balanced | store | 0.189 | 28.3 | 1,084,818 |
| balanced | fast | 0.162 | 31.3 | 524,890 |
| balanced | balanced | 0.295 | 35.1 | 524,890 |
| balanced | maximum | 0.190 | 32.9 | 524,890 |
| maximum | store | 0.165 | 28.2 | 1,084,818 |
| maximum | fast | 0.181 | 31.0 | 524,890 |
| maximum | balanced | 0.306 | 30.7 | 524,890 |
| maximum | maximum | 0.288 | 30.9 | 524,890 |

Compressing presets produced 524,890 bytes versus 1,084,818 for Store on this corpus.
The random part dominates the compressed result. Store deliberately does not compress.
21 mixed/solid/encrypted/header/recovery/volume combinations passed; explicit 2/4/16 MiB dictionary controls also passed.

## Real 20,000-file RAR round-trip

Fast/Eco, 20,000 tiny text files: creation including independent pre-publication verification 150.353 s, sampled CLI+worker RSS 59.6 MiB; archive 1,929,027 bytes.
Extraction 366.457 s, sampled CLI+worker RSS 42.9 MiB; every output name and SHA-256 matched.
This is a correctness/resource qualification, not a speed-target pass. This workload does not isolate filesystem, security-filter or codec costs.
The 20k corpus used the preceding CLI/worker before the publication-only safety correction; both hashes are retained in native-20k.json. Final CLI matrix/read and actual final GUI open are separate checks. The full 20k timing run was not repeated after the publication-only safety correction.
One anonymous rebuild spool avoids an open handle per member; extraction caches at most 128 pinned parents.
These implementation changes do not by themselves prove a universal speedup. Multi-GiB RAR inputs were not qualified.

## 7Z regression control against retained 0.8.0

Same deterministic 16 MiB random source and script; three trials per mode and binary.
The preceding-snapshot 7Z comparison ran sequentially after the other active validation tasks ended. Final publication-only changes were not rebenchmarked. An earlier concurrent run is retained separately and is not substituted for this control.
All 18 independent py7zr SHA-256 round-trips passed. Medians below; direct CLI RSS excludes the Python oracle.

| Mode | 0.8 RSS (MiB) | 0.9 RSS (MiB) | 0.8 wall (s) | 0.9 wall (s) |
| --- | ---: | ---: | ---: | ---: |
| eco | 33.7 | 33.7 | 4.352 | 4.037 |
| balanced | 219.0 | 219.4 | 2.205 | 2.258 |
| maximum | 218.2 | 218.9 | 2.148 | 2.165 |

This control did not show a doubling of 7Z memory. Timing variation on three trials is not a causal speed claim.
Codec settings and worker caps are unchanged; the 0.8 Balanced workspace cost remains explicit.

## GUI and acceptance boundaries

The opt-in 100,000-entry Qt model/table benchmark was executed; its JSON is synthetic-model evidence, not a real 100,000-file RAR test.
Real native-RAR GUI dispatch/journal/worker tests and EN/UK/RU high-DPI screenshots are separate evidence.
Earlier strict project-path publication stress failed on both candidate and retained 0.8; final native replacement has two passing matrices and controlled handle checks. Failed historical evidence remains.
These observations do not override qualification boundaries in ACCEPTANCE_0_9.md. Subsequent CI and scoped Narrator/keyboard results are recorded separately.
