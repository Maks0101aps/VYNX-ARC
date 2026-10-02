# File and decoded-entry hashes

The GUI supports archive-file hashes, selected regular-entry hashes, and
verification of either one selected entry or the whole archive file against a
supplied SHA-256 or CRC32 digest. Hash results can be copied. Entry hashing reads
decoded bytes into a 128 KiB streaming buffer without creating plaintext files.
Selection is validated before decoding. Backend checksum verification and
declared/actual size limits remain active; solid archives may decode other entries
to reach a selection. Cancellation is checked between read chunks.

Expected values must be exactly 64 SHA-256 or 8 CRC32 hexadecimal characters;
case and surrounding whitespace are ignored. Invalid values and mismatches have
distinct HASH_FORMAT / HASH_MISMATCH errors. CRC32 detects accidental corruption;
it does not authenticate a file. A match is only as trustworthy as the supplied
digest and does not certify that archive content is safe.

GUI verification reports MATCH or DOES NOT MATCH, the detected algorithm, actual
digest and supplied value. A mismatch retains the HASH_MISMATCH error code;
archive corruption, cancellation and malformed digests remain separate errors.
Reporting uses the same completed streaming hash, without a second decoding pass.

CLI commands: `hash FILE`, `verify FILE DIGEST`, `hash-entry ARCHIVE ENTRY`,
`verify-entry ARCHIVE ENTRY DIGEST`. Passwords remain GUI-only and are never passed
on command lines. Regression vectors cover abc and empty contents, ZIP/7Z/TAR/
TAR.GZ, ZIP/7Z encryption, incorrect selection, cancellation, malformed digests
and deliberate mismatches. Packaged tests compare entry hashes with Python's
independent SHA-256 implementation and check mismatch exit behavior.
