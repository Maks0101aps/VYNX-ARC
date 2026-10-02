# Windows desktop UI pass

Baseline source: `36451d7`; recoverable screenshot-runner checkpoint: `1c7acaa`.
The baseline build passed 41 Rust tests and both existing native/GUI tests.
48 real Widget captures were recorded before UI changes in `.dev/ui-baseline`:
Home/archive, light/dark, English/Ukrainian/Russian, 100/125/150/200% scale.

## Presentation components

- HomePage: compact identity, primary Open, secondary Create, clear drop target,
  bounded recents with a delegate for filename/path and context actions.
- ArchivePage: vector Back/Up, interactive BreadcrumbBar, responsive search,
  common QAction controls and overflow. Writable actions are selection-aware;
  read-only archives show an explicit status and hide toolbar mutations.
- ArchiveModel: numeric packed/ratio sorting, timestamp sorting and locale display,
  cached totals and folder selection sizes. Name stretches; narrow windows hide
  secondary columns unless the user explicitly selects them in Columns.
- OperationPanel: separate job/path, bytes, smoothed speed and cautious ETA. ETA
  waits for at least three seconds and 1% progress and resets on totals/rewinds.
- ToastOverlay: transient completion/copy notices; persistent archive statistics
  remain in the status bar. Critical failures and decisions still use dialogs.
- Create/Extract/Conflict/Settings/About dialogs own their presentation. MainWindow
  retains archive state, worker ownership, conflict replies and operation orchestration.
- Theme centralizes surface/text/border/selection colors. Original cached vector
  icons retain the VYNX identity; Qt Widgets/model-view and the native frame remain.

Ordinary files open Properties on double-click/Enter; they are not launched.
Folder navigation, shortcuts, drag/drop, password handling, path protection and
codec operations retain the existing core. The only Rust change is exposing
existing timestamp value/validity through the typed presentation bridge.
About reads project version metadata and Qt runtime/build versions.

## Honest limits

Compression displays the actual format default. There are no simulated Fast/
Maximum selectors or unsupported method/thread/solid controls. Backend presets
are a separate remaining task. Advanced creation currently exposes real 7Z split
sizes and password support for ZIP/7Z.

Archive pages remain disabled while a worker owns mutable application state,
preserving the previous safe ownership model. This pass does not introduce a
parallel operation queue or claim broader cryptographic password erasure.

ZIP's default DOS epoch is conservatively displayed as unknown. The archive
metadata cannot distinguish a deliberately supplied 1980-01-01 timestamp from
that default; such legitimate dates are also hidden. Other known timestamps,
including an explicitly known Unix epoch, remain representable and sortable.

The known intermittent ZIP replacement `AccessDenied` recurred twice during
deployed verification; the original was preserved. The next candidate passed the
full independent verifier. No lock retry, security setting or codec behavior was
changed to force that pass, and the cause is still undiagnosed. This is separate
from the UI pass. The earlier intermittent native RAR open is also unresolved.

Screenshot progress/conflict examples are explicit UI fixtures, not throughput
measurements or proof of a live conflict. QWidget grabs exclude native frame/Snap
behavior; screen-reader and clean-VM/Explorer acceptance remain manual checks.
Final screenshot and settled-memory evidence are recorded separately after QA.
