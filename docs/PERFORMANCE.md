# Performance evidence

Measured 2026-10-02 on Windows 11 Pro x64, Ryzen 5 7500F, approximately 32 GiB RAM.
The current deployment uses Qt 6.12.0 MSVC and an optimized Rust core. See
VERIFICATION.json for raw samples.

Small ZIP browser working set sampled one second after process launch was
approximately **41–42 MiB** in each of four combinations of language, theme and
100% / 125% / 200% scaling. The archive contains a small project directory.
This is a single sample per combination, not an idle baseline, a memory ceiling
or proof of large-archive performance. No application processes remained after
the packaged verification run.

The real GUI smoke test exercises 100,000 metadata rows through Qt model/view,
without creating a widget per row. It is a correctness test, not a timed benchmark.

Cold startup, settled idle CPU/memory, million-entry browsing, compression speed,
large-file throughput and SSD/HDD comparisons remain unmeasured. Performance
targets in the master brief have not been accepted based on these samples.
