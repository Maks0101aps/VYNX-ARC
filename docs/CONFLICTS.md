# Extraction conflicts

The GUI defaults to Ask. The worker publishes a bounded conflict request and
waits on a condition variable; the Qt timer opens an asynchronous modal dialog.
The GUI displays both paths, sizes and modification times. Replace, Skip and
Keep both can apply to all subsequent conflicts in this extraction. Cancel or
closing the dialog stops the operation and wakes the worker. Stale responses
cannot answer a later request. Already completed files remain after cancellation.

Keep both generates `name (2).ext`, incrementing the suffix while reserving names
already present in the destination and names planned by the archive. Publication
uses no-clobber semantics, so a late filesystem collision fails safely.
File/directory type collisions are rejected; directories are never recursively
deleted to make room for a file.

Replace if newer compares absolute modification times. ZIP/RAR DOS timestamps
are interpreted in the machine's current local timezone; 7Z/TAR timestamps use
their recorded epochs. A two-second tolerance avoids false replacement caused
by DOS rounding. Unknown timestamps ask the user. Historical timezone ambiguity
and timestamp provenance are limitations; newer does not establish content trust.
Extracted regular files receive their recorded modification time after MOTW
propagation. Directory times and full filesystem metadata are not restored.

The CLI defaults to Stop and also supports `--skip`, `--replace`, `--keep-both`.
Interactive GUI choices are not exposed as unattended CLI waits.

Regression evidence: suffix collisions with another incoming file, skip all,
stale response rejection, cancellation during a pending question, and older/newer
comparisons with output timestamp preservation. The Qt/CXX smoke test exercises
the visible conflict dialog and its Skip/apply-all controls.
