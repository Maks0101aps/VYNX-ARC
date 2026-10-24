# Transactional ZIP / 7Z / RAR5 modification

Add files/folders, delete selected entries (including folder descendants), and
rename files/folders rebuild the archive through bounded streams. No extracted
plaintext workspace is used for ZIP/7Z. RAR5 stages retained bytes in one anonymous
delete-on-close spool. Duplicate/case-colliding paths fail before rebuilding.
Single-volume unlocked/unsigned RAR5 supports the same actions and working-copy
save-back. RAR4, multipart RAR and other formats remain read-only. RAR details
and metadata limits: [NATIVE_RAR.md](NATIVE_RAR.md).

The original is opened with a Windows sharing mode that permits readers but denies
writes/deletion while it is decoded. The replacement is a unique sibling temporary
file on the same volume. It is finalized, flushed, reopened, fully decoded and its
manifest compared before publication. Cancellation or decoding/verification errors
remove the temporary file. A locked-target publication failure preserves the original.
No cancellation is reported after the final commit decision, so a completed commit
always refreshes the GUI. Mark-of-the-Web is copied to the replacement.

Publication keeps the existing bounded Windows retry classification and uses
native `ReplaceFileW` with a unique same-volume `.vynx-recovery-*` backup.
Writable staging handles are closed before verification and publication. Metadata
merge flags are zero: ACL/attribute transfer errors are never ignored. There is
no fallback that deletes the source or renames over a missing source.
On Windows partial-move errors 1176/1177 both backup and verified replacement are
retained and `RECOVERY_NEEDED` reports their paths across the worker boundary.
The backup is removed after successful publication; a supervisor/host crash can
leave a recovery sibling. Manual inspection is required before deleting it.
The native semantics and partial-error cases are documented by
[Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew).

The final candidate has repeated passing strict project-path matrices and
controlled compatible/incompatible-handle checks. Historical MoveFileExW error-5
failures remain in the evidence. This qualifies the new native replacement;
it does not identify which external process caused every historical failure.
See [ACCEPTANCE_0_9.md](ACCEPTANCE_0_9.md).

Outputs are recompressed using the current ZIP/7Z defaults, not a raw block edit.
ZIP archive comments and file modification dates, and retained 7Z file timestamps
are preserved. Auxiliary ZIP metadata, directory timestamps and exact compression
parameters are not yet comprehensively preserved. Do not call this production-quality
metadata preservation. The source handle must be released before path replacement;
the same hostile local-process race caveat as extraction still applies.

Preflight uses a conservative two-times-uncompressed-size plus original-archive-size
estimate, with 5% / minimum 64 MiB safety margin. It is not a disk-space reservation.
The streaming writers and final flush still propagate disk-full / IO errors.

Tests cover ZIP/7Z and encrypted variants, Unicode, folder renaming/deletion,
duplicates, unsafe renames, wrong passwords, cancellation during data processing,
locked targets, empty archives, long archive paths and corrupted source payloads.


## 0.5 editable working copies

The GUI provides a separate explicit edit workflow for supported document/media
files. This does not change read-only external opening. Working copies live in
EditStore, survive restarts and are never deleted by disposable-view cleanup.

Replace validates the complete source archive SHA-256 and working-file SHA-256
while denying write/delete access to those files. A changed source, busy editor,
invalid entry, cancellation, disk-full or publication failure preserves the original
and the working copy. Save returns the verified replacement digest computed before
commit. The GUI refreshes the owning tab and stores a new edit baseline. If writing
recovery metadata fails after a successful save, the user is told to reload first.

Reload source only accepts an unchanged original entry payload. Changes elsewhere
in the archive can then be reviewed and saved against a new container digest;
conflicting entry edits require export rather than an implicit merge. An editor
that writes only part of a file and closes all handles cannot be distinguished from
an intentional edit: the user must finish editing before explicitly saving.

Rebuilds honor compression/resource presets. ZIP preserves supported timestamps and
encryption but may normalize compression method. 7Z preserves supported metadata and
encryption; solid grouping/exact physical layout is not preserved. Split archives
and other formats remain read-only. Editable materialization is limited to 256 MiB.
