# zip 9.0.4 local patch

Source: crates.io `zip` 9.0.4, MIT. Retained library sources, normalized manifest,
README and license. No codec/crypto changes.

Registry crate SHA-256:
`a954114b47a54df49f83d2722814e86b23d4bb1b54a1b90fb7d92b41619ddc22`.
Published source revision: `7d9988688723f1136966e0e97200f41356dc57eb`.

Adds `ZipArchive::with_preflighted_directory`: takes the exact bounded directory
offset/count/comment from VYNX's ZIP32/ZIP64 preflight, uses normal central header
parsing, refuses duplicate names, and never searches for a different end record.
This fixes Store ZIPs containing nested ZIP end signatures without removing
ambiguity protection or allowing backend fallback to an unbounded directory.
ZIP64 extensible metadata is deliberately not retained by this application path.

Upgrade: compare the small extension in `src/read/zip_archive.rs`, retain license,
update crate provenance, rerun parser_limits (nested ZIP, ambiguous end records,
ZIP64, counts, truncated/corrupt metadata), full source and package gates.
