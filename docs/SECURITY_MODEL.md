# Security model

Archives and their metadata are attacker controlled. The destination directory and
source files are user owned. Operations run as the current user, with no elevation.

Reject traversal, absolute/UNC/drive paths, ADS, reserved DOS names (including
superscript COM/LPT names), control characters, trailing dots/spaces, empty components,
oversized paths, duplicate names and case-insensitive collisions. Reject links,
reparse points and file/directory prefix conflicts. Never execute extracted content.
Never recursively extract embedded archives. Do not strip Mark-of-the-Web.

Apply checked size arithmetic and count / individual / total output limits before
and during decoding. CRC is integrity checking, not authentication. Passwords are
ephemeral; application-owned password buffers use zeroization and are never logged.
Backend allocations and password copies require separate review.

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
