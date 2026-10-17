# Desktop interface

The current product is a native Qt Widgets archive workspace. Historical screenshot
counts and UI-only milestones are recorded in their dated reports; current acceptance
belongs to [MILESTONE_0_5.md](MILESTONE_0_5.md).

- Archive tabs keep folder, query, selection, columns and scroll separately. Ctrl+Tab,
  Ctrl+Shift+Tab and Ctrl+W switch/close tabs. Duplicate paths activate the existing
  tab. Busy owners cannot be edited; other tabs remain navigable and previewable.
- ArchivePage has responsive toolbars, a breadcrumb and archive-wide name/path search.
  F3/double click previews; Ctrl+Enter creates a confirmed read-only external copy.
  Ctrl+Shift+Return creates a separate editable working copy.
- View > Working copies (Ctrl+Shift+W) shows source, entry, state and retained errors.
  Changes are marked with an asterisk on their archive tab. Save, reload source,
  export and discard are explicit. The toolbar overflows in narrow layouts.
- View > Temporary files shows disposable viewing copies and their byte totals.
  Editable copies are listed separately and cannot be removed by this cleanup.
- The queue supports pause, reorder, cancellation, detailed errors, review/retry and
  JSON report export. Restored jobs are interrupted, never automatically replayed.
  Batch totals distinguish completed/stopped/remaining operations.
- Compression and resource selectors apply to creation and ZIP/7Z rebuilds. Displayed
  limits reflect backend settings. OperationPanel displays phase, bytes, speed and
  cautious ETA; terminal results remain in the queue.
- Themes and EN/UK/RU translations cover product controls. The screenshot runner uses
  explicit fixtures in isolated storage and records application hashes. Qt scale
  captures do not prove a change to the Windows desktop's physical DPI setting.

Working copies retain decrypted content and source paths on disk until discarded.
The consent dialog explains this before preparing a copy. Executables, scripts,
shortcuts and active web content remain excluded from direct external opening/editing.
External applications may create their own copies outside VYNX's management.

ZIP's default DOS epoch is displayed as unknown; legitimate 1980-01-01 timestamps
cannot be distinguished from this format default. Status indicators use metadata
fingerprints; a save always rechecks cryptographic digests and current source locks.
