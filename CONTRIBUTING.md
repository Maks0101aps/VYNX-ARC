# Contributing

Read docs/ARCHITECTURE.md, docs/SECURITY_MODEL.md and docs/BUILD.md first.
Build with scripts/build.ps1. Rust: cargo fmt, cargo clippy and cargo test.
C++: clang-format using .clang-format. Add regression tests for security and
data-integrity behavior. Keep GUI workers asynchronous and core validation in Rust.
Do not add telemetry, background services or external archiver subprocesses.
Third-party licenses must be reviewed and retained when dependencies change.

Submit changes through a pull request. Do not commit archives containing real
private data, credentials, signing keys, build output or development SDKs.
