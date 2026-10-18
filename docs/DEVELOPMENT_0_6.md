# 0.6 development evidence

This is an incremental development record, not release acceptance. Versions remain
0.5.0 until a validated 0.6 candidate is ready.

## Selected extraction

Moved destination path inspection after selection filtering. Full archive plan
validation and decoding/CRC checks of unselected payloads are retained. A regression
checks that an unrelated destination ancestor which is a regular file neither blocks
selected extraction nor gets modified.

First three-trial release benchmark on the same 100,000-entry ZIP: selected extract
median **1.735 s**, versus **40.141 s** in the 0.5 report. Raw results:
[JSON](benchmarks/0.6/many-core-first.json). This is core timing, not complete GUI
latency. Other Rust checks overlapped part of this run; opening was 1.369 s versus
1.221 s previously (+12.2%), so isolated comparison is required before acceptance.
The harness also passed cancellation and its existing 100 small-archive retention
cycles (137 handles before/after); these are not the planned complete GUI cycles.

Validation: workspace Clippy with warnings denied; all 78 Rust tests passed,
one opt-in benchmark ignored by the ordinary suite; release benchmark passed.
