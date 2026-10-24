# Native RAR5 encoding in 0.9.0

Candidate implementation; see [acceptance](ACCEPTANCE_0_9.md) for the exact
qualified build and open release gates. No external archiver is a runtime
dependency. Windows x64 remains the supported product target.

## Dependency audit and API

The independent encoder is exact crates.io `rars = 0.12.0`, Apache-2.0,
[bitplane/rars](https://github.com/bitplane/rars). Upstream source was inspected
at `15ef2c69d64ae8a670962175ce6b69277894e4a2`; Cargo.lock identifies the
registry archive actually compiled. The reviewed facade, streaming writer,
workspace accounting, source factories, progress/cancellation, volume sink,
crypto and recovery implementations contain working code, not only advertised
features. Context7 had no relevant rars entry; the published crate sources and
upstream integration tests were the API references.

VYNX uses `rar50::write_streaming_archive_with_progress`,
`write_streaming_volumes_with_progress`, `ArchiveEntry`, `EntrySource`,
`WriterOptions`, `ArchiveExtras`, `VolumeSink` and `WriterResources` directly.
The facade `Builder` exposes several options, but its dictionary setter is
private in this version; the public RAR5 streaming writer exposes it correctly.
Only write/encryption/recovery features are enabled. Rayon/parallel compression,
RAR7/legacy encoding, SFX, quick-open and recovery repair are not exposed.

The existing official UnRAR 7.23 adapter remains the reader for RAR4/RAR5. It
only uses native list/skip/test operations and callback bytes; no compressor was
implemented from UnRAR source. This also makes pre-publication decode independent
of the encoder. No `rar.exe`, `WinRAR.exe` or other external archive executable
is discovered or launched by application code. Test harnesses may explicitly use
a separately installed WinRAR or Windows libarchive `tar.exe` as independent
oracles; neither is bundled.

## Available controls

- Store/Fast/Balanced/Maximum map to real RAR5 levels 0/1/3/5. Store copies data.
  The compressing presets produce smaller outputs on the qualified mixed corpus.
- Optional solid compression. It can require decoding preceding members when
  selecting a file; it is not a promise of random-access extraction.
- Automatic dictionaries: Fast 1 MiB, Balanced 8 MiB, Maximum 32 MiB, capped at
  2/8/32 MiB by Eco/Balanced/Maximum resources. Explicit powers of two from
  128 KiB to the mode cap are accepted by core; GUI/CLI expose whole MiB values.
- AES-256 member encryption and optional encrypted filenames. Header encryption
  requires a password. Passwords use the existing private worker IPC; jobs and
  settings retain only a password-needed flag. CLI `--password-stdin` reads one
  UTF-8 line of at most 4096 bytes, never a password command-line value.
- Embedded recovery records: zero disables, 1..10 percent enables. VYNX can
  generate and integrity-test these archives. VYNX does not offer recovery repair.
  Independent WinRAR repair of deliberately damaged stored data is a separate
  qualification case; reading valid archives alone does not prove repair.
- Native RAR5 volumes: choose `name.rar` as a base; outputs are `name.part1.rar`,
  `name.part2.rar`, etc. The selected byte count is the payload budget; headers,
  encryption and recovery add overhead. Minimum 64 KiB payload, maximum 4096
  parts. These are actual RAR volume headers, not sliced single-file archives.
- Add/delete/rename and explicit working-copy save-back rebuild single-volume,
  unlocked, unsigned RAR5. RAR4, multipart, signed and locked archives stay
  read-only. The GUI receives this capability from native metadata. Conversion
  remains into ZIP/7Z. Rebuilds retain solid/header-encryption flags and generate
  3% recovery when the source had recovery and no percentage was selected;
  original recovery density and arbitrary metadata are not preserved.

## Safety, resources and publication

All inputs use the common Windows-aware plan validation: no links/reparse points,
unsafe paths or case collisions; checked sizes and count limits. Source readers
check size/mtime and hold write/delete-denying handles while consuming input.
RAR reader paths normalize separators so nested selections and working-copy
save-back address the same entries. Typed callback errors retain missing-volume,
cancellation and resource-limit codes across `std::io` adapters.

Managed encoder memory is capped at 256/384/1024 MiB. RAR's Eco budget is distinct
from the other codecs' 64 MiB estimate: rars' level-5 block parser can require
about 197 MiB with a 2 MiB dictionary. Preparation/header quotas are 64/32 MiB;
logical encoder spool content is capped at the common 256 GiB total limit.
These accounting budgets exclude application input descriptions, allocator
overhead and other processes. GUI and RAR CLI create/modify use supervised
workers with separate 768/1536/3072 MiB Windows committed-memory limits.

Rebuild plaintext uses one anonymous delete-on-close spool with bounded range
readers, rather than one open file per member or whole files in RAM. The worker
supervisor owns the backend workspace and removes it after cooperative or forced
worker termination. A supervisor/host crash may leave backend spools; this is
not encrypted temporary storage or secure erasure. Pagefiles/dumps and backend
password copies are not comprehensively zeroized. Common temporary storage
limitations still apply. No automatic retry deletes source or working files.

Writing stages unique files, syncs and closes writable container handles before
independent decode, checks the normalized manifest, then uses no-clobber creation
or the existing replacement transaction. Existing files and source archives are
never deleted to make publication succeed. Volume sets are fully verified before
the first publication; publication of multiple files cannot be atomic. Failure
rolls back only file objects created by that operation, using handle identity.
Unknown worker write results continue to require explicit result verification.

## Distribution

The [Apache-2.0 text](https://www.apache.org/licenses/LICENSE-2.0) is copied from
the crate's COPYING file into packaged licenses, along with transitive notices.
Application MIT licensing does not replace dependency licenses. The existing
UnRAR decoder's separate terms continue to apply to that decoder. No WinRAR
binary or license is redistributed. This records the declared licenses and
integration boundaries; it does not independently certify upstream provenance.
