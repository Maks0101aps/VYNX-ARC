# VYNX ARC 0.5.0 development milestone

Implemented from `v0.4.0` on 2026-10-17. Product source is committed as
`fdbb0f52abe80e802f6fd5a69741e843cc905680`; the release documentation commit and
`v0.5.0` tag retain those validated binaries. This is a development milestone,
not completion of the full stable V1 contract.

## Delivered workflows

1. **Archive workspace.** ArchiveSession owns each document's model/filter, path,
   password in memory, navigation, query, selection, columns and scroll. Tabs,
   duplicate/volume normalization, deferred close/cancel and workspace restoration
   are implemented. Another idle tab remains searchable and previewable during a job.
2. **Recoverable queue.** Typed descriptions, bounded atomic journal with an owner
   lock, pause, decision/running/terminal/interrupted states, explicit review/retry,
   editable parameters, linked attempts, batch counts, output-folder action and JSON
   reports. No password or content is journaled. Restored writes never auto-start.
   Selections beyond the journal's 10000-item limit are marked incomplete and cannot
   silently replay as a smaller selection; the user must repeat them from the tab.
3. **Edit and save back.** Separate consented editable copies, explicit verified
   ZIP/7Z content replacement, supported encryption/metadata and resource presets,
   source/input digest conflict detection, save receipt before publication, owning-tab
   reload, retained copies after failure/restart, reload-source/export/discard actions.
4. **Measured performance.** Selected ZIP reads avoid decoding unrelated payloads.
   Metadata preparation runs on a worker; cached model rows, bounded folder cache,
   direct filtering and locale sort keys reduce search cost. See [benchmarks](BENCHMARKS_0_5.md).
5. **Completed data controls.** Temporary-view manager and separate editable-copy
   manager, visible pending-edit markers, EN/UK/RU, keyboard tab commands and responsive
   toolbars. Current architecture, format capabilities and security limits are documented.

Read-only external opening remains separate from editing. Editable copies can retain
plaintext and source paths even with recent-history storage disabled: this is explained
before creating one. Nothing monitors editor closure as an implicit save instruction.

## Validation

The [machine-readable report](VERIFICATION_0_5.json) records exact hashes, source
identity, report paths and scopes. Local evidence is retained under `.dev/milestone-0.5`.

- Mandatory packaging gates: Rustfmt, Clippy `-D warnings`, 77 Rust tests, dependency
  audit denying yanked versions, three CTest targets. The Qt suite has 21 passes
  including init/cleanup, plus one intentionally skipped opt-in performance case.
- Real GUI bridge roundtrips, three documents including an encrypted archive,
  independent state and background preview, 60 tab switches without selection-model
  accumulation, and a completion callback starting another owned job.
- Editable copy changed by a separate Windows process, explicit save and fresh-archive
  verification; a conflicting source change preserves both source and unsaved edit.
  Process-killed journal/EditStore recovery is tested through a real child process.
- Two real CLI process kills at observed rebuild/late-staging boundaries preserve
  the original archive; six cross-process busy-target/refuse/retry cases pass.
- 512 seeded parser mutations under a 512 MiB job limit; 4900 ZIP publications
  across add/delete/rename/mixed scenarios with and without Zone.Identifier, no failures.
- Final portable GUI/CLI and independent archive readers with only Windows directories
  on PATH. ZIP/7Z and the existing format, Unicode, MOTW and cancellation checks remain.
- Real per-user installer upgrade 0.4 -> final 0.5, exact installed application hash,
  retained settings, installed GUI/CLI execution, and registered uninstall command.
  Optional shortcut/Open With/Explorer tasks were disabled in this isolated-directory
  development-host check. It does not qualify a clean Windows machine.
- Final binary: 96 new-screen captures (four states, EN/UK/RU, two themes and
  Qt scale 1/1.25/1.5/2), plus 24 main-window captures at 620x440. Modal dialogs retain
  their default sizes in that narrow-window test; the latter qualifies main-window
  overflow/navigation, not every manually resized modal. Capture fixtures illustrate
  layout; their synthetic queue rows are not evidence of runtime job counters.

## Performance and remaining boundaries

Search p95 on the synthetic 100k model: **289.89 -> 62.70 ms**. Model preparation
increases **18.64 -> 84.29 ms**, now outside the UI thread. GUI sampled RSS is about
95 MiB in the model benchmark; core sampled peak on 100k ZIP is about 165 MiB.
Selected-file preview in the large ZIP improves **9.75 -> 0.19 ms**. Selected extraction
of 100k ZIP remains about 40 seconds and needs a more efficient metadata/filesystem
preflight. Full raw medians, sampling scopes and the small solid-7Z opening variation
are retained rather than replaced with favorable reruns.

No complete parser allocation budget, hostile-local-process filesystem race freedom,
universal decoder cancellation bound or exact preservation of all archive metadata is
claimed. Edit materialization is limited to 256 MiB, and split/other formats remain
read-only for save-back. A partially written file with all editor handles closed cannot
be distinguished from an intentional edit: finish editing before explicitly saving.

Physical power-loss testing, a kill precisely inside the Windows publication syscall,
manual qualification of particular desktop editors, actual Windows DPI changes,
multimonitor accessibility, clean-machine/enterprise policy and signed distribution
remain separate qualification. Qt scale captures do not prove physical DPI changes.
Binaries are unsigned. See [status](IMPLEMENTATION_STATUS.md),
[roadmap](ROADMAP_0_5.md), [modification](MODIFICATION.md) and [security](SECURITY_MODEL.md).

The first acceptance run of the tab-selection stress found retained header selection
models. The final implementation releases only models unused by the table/child views;
the repeated native and deployed smoke tests pass. Earlier candidate logs and the
benchmark calibration remain available; failed intermediate runs are not final passes.
