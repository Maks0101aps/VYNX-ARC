# VYNX parser-budget patch

Upstream: https://github.com/hasenbanck/sevenz-rust2, crate 0.23.0.
Crate download: https://static.crates.io/crates/sevenz-rust2/sevenz-rust2-0.23.0.crate
Published crate SHA-256:
`0a4f883677093690e91fef8ae81fad8bce9e2c3a079b61054f05ce0d3ecf681e`.
Source and Apache-2.0 license are retained. Cargo.toml.orig is the upstream
manifest. Only the library is built; upstream examples/tests are not vendored.

Local changes are confined to Cargo.toml, src/archive.rs, src/reader.rs and
src/decoder.rs (plus this provenance document):

- Use rlib only; omit unavailable example/test targets.
- Limit the raw next header and the decoded encrypted/compressed header to 16 MiB
  before allocation/decompression.
- Charge a conservative cumulative 128 MiB metadata budget before entry, block,
  coder, stream, name/property and index/map allocations. The accounting includes
  headroom for transient bitsets and cloned names, not allocator instrumentation.
- Reject coder chains longer than eight before building nested decoder readers.
- Budget LZMA/LZMA2 dictionaries and PPMD arenas to 64 MiB per live coder chain
  before decoder construction. This is independent of unpacked file-size limits.
- Keep defensive checks at individual LZMA/LZMA2/PPMD decoder construction sites.

These budgets deliberately reject some very large otherwise valid archives.
They do not cap GUI copies, overall process RSS, all codec workspaces, all CPU
work, or native RAR/TAR allocations. No process sandbox is added to the product.
The fuzz smoke runner has a separate test-only Windows Job memory limit.

For an upgrade, compare these four files to the verified upstream crate, rebase
the budget changes, update the checksum here, then run scripts/check.ps1,
parser_limits regressions, encrypted round trips, mutation smoke and packaged
independent-reader verification. Never drop the patch during a Cargo update.
