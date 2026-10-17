# Implementation status - 2026-10-17

Version: **0.5.0 development milestone**. This is not acceptance of the entire V1
contract. Current evidence belongs to [MILESTONE_0_5.md](MILESTONE_0_5.md) and
[VERIFICATION_0_5.json](VERIFICATION_0_5.json). Earlier reports, including 0.4, are
historical and retain their original pass counts and failures.

## Available behavior

- Offline native Qt/CXX/Rust GUI and CLI; ZIP, 7Z, RAR4/RAR5, TAR and compressed
  TAR/standalone streams. RAR remains read-only. See [formats](SUPPORTED_FORMATS.md).
- Independent archive tabs with folder history, search, selection, columns and scroll;
  duplicate/split-volume path normalization, busy-tab cancellation, opt-in workspace
  restoration. Another idle tab can search/navigate/preview during a primary job.
- One sequential writer queue, bounded durable job descriptions, atomic journal,
  explicit review/retry after interruption, parameter changes, password re-entry,
  batch counts, clearable history and JSON report export. Oversized selections
  cannot silently replay from truncated descriptions. Restored writes never
  resume automatically. Size/mtime source snapshots are review hints only.
- ZIP/7Z add/delete/rename and explicit edit/save-back. Separate registered working
  copies retain failed/conflicting edits across restart. Save validates source and
  input SHA-256 under locks, verifies the replacement and updates the owning tab.
- Read-only text/raster/Hex preview; selected ZIP payload read without decoding
  unrelated payloads. Full testing still validates every member. External viewing
  is explicit and restricted; executable/script/active-content types are excluded.
- Disposable-view manager, editable-copy manager, visible pending-edit indicators,
  EN/UK/RU, light/dark/system theme, keyboard commands and responsive toolbars.
- Compression/resource presets for creation and rebuild, phase progress/cancellation,
  conflict decisions, Smart Extract, file/entry hashes and split-7Z creation/read.
- Complete metadata/path/collision validation before output; per-file publication,
  bounded decoder output, link/reparse rejection, conservative disk preflight.

## Boundaries

Working copies keep plaintext and source paths until explicitly discarded, including
when recent-history storage is disabled: creating a copy requires separate consent.
The visible edit fingerprint uses metadata; authoritative save checks use hashes.
Edits are limited to 256 MiB and ZIP/7Z without volumes. No auto-merge, silent save,
external-editor termination detection or recovery from physical power loss is promised.

Parser libraries can allocate metadata before all application-level entry budgets
are checked. This version does not establish complete parser memory budgets or
hostile-local-process filesystem race freedom. See [security](SECURITY_MODEL.md).
Solid codecs may decode shared blocks, and cancellation has backend-dependent bounds.

The binaries remain unsigned. Local package/installer checks cannot establish
SmartScreen reputation, enterprise-policy behavior or clean-machine qualification.
Windows desktop DPI and Qt scale-factor captures are distinct forms of validation.

## Next development priorities

1. Parser memory budgets and sustained malformed-input fuzzing with instrumented builds.
2. Signed distribution and trusted update/recovery policy.
3. Broader independent Windows/real-editor/DPI accessibility testing.
4. Faster selected extraction preflight for very large metadata sets, richer editor
   conflict recovery, and measured optimization of the remaining solid/stream paths.
