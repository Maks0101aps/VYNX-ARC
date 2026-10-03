# Transactional ZIP / 7Z modification

Add files/folders, delete selected entries (including folder descendants), and
rename files/folders rebuild the archive through bounded streams. No extracted
plaintext workspace is used. Duplicate/case-colliding paths fail before rebuilding.
RAR and the other formats remain read only for modification.

The original is opened with a Windows sharing mode that permits readers but denies
writes/deletion while it is decoded. The replacement is a unique sibling temporary
file on the same volume. It is finalized, flushed, reopened, fully decoded and its
manifest compared before publication. Cancellation or decoding/verification errors
remove the temporary file. A locked-target publication failure preserves the original.
No cancellation is reported after the final commit decision, so a completed commit
always refreshes the GUI. Mark-of-the-Web is copied to the replacement.

Publication makes one attempt through pinned `tempfile::NamedTempFile::persist`.
On Windows this first clears the temporary attribute with `SetFileAttributesW`,
then calls `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`. Failures are reported without
retry/sleep masking. The historical location-associated error 5 remains
unresolved; see [PUBLICATION_DIAGNOSTICS.md](PUBLICATION_DIAGNOSTICS.md).
Passing stress runs alone do not identify or fix that historical failure.

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
