# Product development - 2026-10-16

Historical format/resource increment, superseded by [0.4](MILESTONE_0_4.md).

Current development checkout; public version remains 0.1.0. This increment
selectively integrates the feature worktree into the newer hardened main code.
No release tag, commit, publication or remote CI run was made for this increment.

## User-visible changes

- Create, browse, test, preview text and extract standalone GZIP, XZ, BZIP2,
  ZSTD and LZMA streams. A standalone stream takes exactly one regular file;
  the creation dialog explains this and disables adding folders.
- Create/read TAR.XZ, TAR.BZ2 and TAR.ZST for folders and multiple files.
- Compression choices Store/None, Fast, Balanced and Maximum control real
  codec settings. Unsupported Store combinations are disabled; plain TAR
  selects Store. ZIP/7Z Store still supports the selected password protection.
- Resource modes Eco, Balanced and Maximum constrain supported codec workers,
  dictionaries/windows and estimated encoder workspace. Settings > Performance
  chooses the default; each creation dialog can override it. Eco temporarily
  lowers the Windows worker thread priority and restores it afterward.
- The operation queue dock can be opened through View. Open multiple archives
  or submit another archive/Explorer request during work to queue it. Work runs
  sequentially. Select a pending row to cancel it; Clear completed removes
  finished history. There are at most 32 pending requests and 64 finished rows.
- Progress shows the current phase, configuration and cancellation state, with
  rate and ETA restarted when the phase changes.
- New controls are translated into English/Ukrainian/Russian; backend technical
  configuration/error strings can still be English.
- Optional installer Open With registration includes the new stream extensions.

Example CLI usage:

```powershell
vynxarc-cli create Project.tar.zst Project --preset fast --resource eco
vynxarc-cli create notes.txt.xz notes.txt --preset maximum --resource balanced
vynxarc-cli test notes.txt.xz --resource eco
```

## Validation

The full source/package gates pass: Rust formatting, Clippy with warnings denied,
70 Rust tests, cargo audit denying yanked dependencies, native Release build and
3/3 CTest targets (presentation, shell smoke, GUI smoke). The GUI smoke executes
queued archive requests and cancels a pending request; screenshot fixtures are
separate from that runtime check.

The portable package is checked with only Windows/System32 on PATH and developer
Qt variables removed. Round trips cover all 12 writable formats, independent
Python readers cover ZIP/7Z/TAR variants, and all five standalone codecs are
checked in both directions against independent Python encoders/decoders.
Existing split-7Z, RAR multipart, Smart Extract, ZIP/7Z modification and entry
hash scenarios remain covered.

A deterministic 256-case mutation smoke run across all 12 writable format seeds
passed with child time/memory limits. This is not coverage-guided fuzzing.
Six cross-process lock/preservation/explicit-retry checks passed. The separate
project-path publication matrix ran 100 cycles per category with and without
Mark-of-the-Web; see its JSON for exact totals. These results do not explain
historical publication failures.

Local evidence is under `.dev/product-development/`: package-final.log,
portable-verification-final.json, fuzz/results.json, locks/results.json,
publication-stress/results.json and screenshots/. These ignored local reports
identify the tested binaries/package. The final portable SHA-256 is
`d584dfc3c97fd6c637822dfe54b23b7cad70ac2835812c4016c8b4160cb4f731`.
The CLI is byte-identical across the final UI-only rebuild, so the preceding
mutation, lock and 1,400-publication results apply to the same CLI binary. Artifacts are in `dist/` with SHA256SUMS.txt.

## Boundaries and next product work

Resource modes are codec configuration limits, not whole-process memory quotas.
Modifying an existing ZIP/7Z keeps the previous rebuild compression policy;
creation presets do not configure modification. Queue history is in memory,
requests are not persisted or reordered, and jobs requiring user choices can
open their normal dialogs when dispatched. Creating archives from the toolbar
still requires finishing the current job. Reading compressed-stream metadata
requires decoding it; compressed TAR identification uses USTAR metadata.

Next product priorities: safer external opening of extracted files, richer
preview, queue reordering/batch extraction controls, and measured large-archive
performance. Preserve the current path checks, parser budgets, encryption and
transactional replacement while extending these workflows.
