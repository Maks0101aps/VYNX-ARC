# Security model

Archives and their metadata are attacker controlled. The destination directory and
source files are user owned. Operations run as the current user, with no elevation.

Reject traversal, absolute/UNC/drive paths, ADS, reserved DOS names (including
superscript COM/LPT names), control characters, trailing dots/spaces, empty components,
oversized paths, duplicate names and case-insensitive collisions. Reject links,
reparse points and file/directory prefix conflicts. Never execute extracted content.
Unicode normalization-equivalent names are not normalized or collision-checked;
that remains unresolved. Uppercase case-collision checks are not a complete
model of every Windows/Unicode filesystem identity rule.
Never recursively extract embedded archives. Do not strip Mark-of-the-Web.

Apply checked size arithmetic and count / individual / total output limits before
and during decoding. CRC is integrity checking, not authentication. Passwords are
ephemeral; application-owned password buffers use zeroization and are never logged.
Backend allocations and password copies require separate review.

ZIP directory preflight runs before ZipArchive construction on open, decode and
modification paths. It caps directory bytes at 32 MiB and conservative metadata
accounting at 256 MiB (1024 bytes per declared entry plus four times directory
bytes), validates ZIP64 ranges and scans fixed-size entry headers. Offset guessing,
multi-disk ZIP, ambiguous end records, trailing data and non-contiguous/signed directories are rejected.
These are explicit compatibility restrictions. See core/src/parser.rs.

The patched 7Z reader caps raw/decoded headers at 16 MiB, cumulative metadata
accounting at 128 MiB and LZMA/LZMA2/PPMD coder-chain dictionary/arena accounting
at 64 MiB, with at most eight coders. Limits are checked before affected parser
and decoder allocations, including encrypted/encoded headers. They are not an
overall RSS ceiling or a complete audit of every codec allocation. TAR/PAX,
native RAR allocation budgets, ZIP codec workspaces and CPU budgets remain open.

Text preview decodes with the same stream validation and output limits, accepts
only UTF-8 text up to 1 MiB and displays it as plain text in a read-only widget.
It writes no temporary plaintext file and launches no external application.
For solid archives, decoding unrelated streams can still take significant time.

Sibling temporary output is published after stream/checksum completion; cancellation
removes unpublished temporary files and preserves pre-existing files. A partially
completed extraction may contain already completed files; it is not an all-or-nothing
directory transaction. Defaults never overwrite existing files.

Existing path components are inspected for reparse points and pinned with Windows
handles that deny deletion/renaming while output is published. Junction rejection
and blocked rename tests pass. This is not a proof against all hostile local-process
races. A release requires further handle-relative filesystem review and adversarial
race tests. Native backend parsers
also require resource-limit and vulnerability audits. Do not call this release secure
or production ready until those gates are satisfied.
