# 0.9.0 implementation and qualification

Implemented, packaged and qualified on the development host and GitHub Actions. Final behavior,
110 Rust tests, independent decoder evidence, measurements and unresolved gates
are in [ACCEPTANCE_0_9.md](ACCEPTANCE_0_9.md). This plan is retained as the audit
sequence; remote CI passed on ed65934; clean interactive Windows and public distribution are not claimed.

Baseline: `release/0.8.0` at `1225285`, initially clean; work is isolated on
`development/0.9.0`. Push is authorized for CI qualification; tags and release publication remain unrequested.

1. Audit: Rust core, CXX, typed GUI jobs, worker framing, source locks,
   transactional publication, format policy, tests, packaging and Windows CI.
2. Add exact `rars 0.12.0` (Apache-2.0) encoding only. Keep UnRAR decoding
   independent; never use UnRAR source to implement compression.
3. Integrate RAR5 presets, bounded dictionary/workspace, solid, AES/header
   encryption, recovery generation and native volume framing where verified.
4. Reuse manifest/path/output checks, cancellation, supervised jobs and staged
   publication. Rebuild modifications without touching originals before verify.
5. Regression and adversarial tests, independent decoding, fuzz smoke, GUI
   tests, measured benchmarks, source gates and deployed package checks.
6. Produce local Portable/Setup/MSIX candidates, notices and an acceptance
   report with explicit failures/unexecuted gates. Never infer stability from
   partial checks or replace old evidence with newer successful runs.

Baseline open gates: clean Windows guest (host hypervisor unavailable), complete
manual keyboard workflow, final-tree CI. ZIP stored nested archive rejection
needs a parser/backend binding fix, not removal of ambiguity protection.

Resource profiles constrain codec workspaces; they are not process RSS caps.
Multi-file volume publication cannot be atomic. Existing working-copy and journal
formats must remain readable. New job parameters must have compatible defaults.
