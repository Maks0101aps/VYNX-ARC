//! Hash decoded entries without staging plaintext files.
use crate::{ArcError, Archive, Operation, Result, operations::copy_checked, security::Policy};
use sha2::{Digest, Sha256};
use std::{
    collections::HashSet,
    io::{self, Write},
};

#[derive(Debug)]
pub struct EntryHash {
    pub name: String,
    pub sha256: String,
    pub crc32: String,
}
#[derive(Default)]
struct Sink {
    sha: Sha256,
    crc: crc32fast::Hasher,
}
impl Write for Sink {
    fn write(&mut self, data: &[u8]) -> io::Result<usize> {
        self.sha.update(data);
        self.crc.update(data);
        Ok(data.len())
    }
    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}
pub fn entries(
    archive: &Archive,
    selected: &[u64],
    password: &str,
    op: &Operation,
) -> Result<Vec<EntryHash>> {
    let policy = Policy::default();
    op.check()?;
    let total = archive.validate(&policy)?;
    if selected.len() > policy.max_entries {
        return Err(ArcError::new("LIMIT", "Too many selected entries"));
    }
    let ids: HashSet<_> = selected.iter().copied().collect();
    let available: HashSet<_> = archive
        .entries
        .iter()
        .filter(|e| !e.directory)
        .map(|e| e.id)
        .collect();
    if ids.iter().any(|id| !available.contains(id)) {
        return Err(ArcError::new("SELECTION", "Select regular files to hash"));
    }
    let mut hashes = Vec::new();
    let mut actual = 0;
    op.total(total);
    archive.streams(password, op, |entry, data| {
        op.phase(&entry.name);
        if ids.is_empty() || ids.contains(&entry.id) {
            let mut sink = Sink::default();
            copy_checked(data, &mut sink, entry, op, &policy, &mut actual)?;
            hashes.push(EntryHash {
                name: entry.name.clone(),
                sha256: sink
                    .sha
                    .finalize()
                    .iter()
                    .map(|v| format!("{v:02x}"))
                    .collect(),
                crc32: format!("{:08x}", sink.crc.finalize()),
            });
        } else {
            copy_checked(data, &mut io::sink(), entry, op, &policy, &mut actual)?;
        }
        Ok(())
    })?;
    Ok(hashes)
}
/// The expected value is a single SHA-256 or CRC32 hexadecimal digest.
pub fn verify(sha256: &str, crc32: &str, expected: &str) -> Result<()> {
    verification_report(sha256, crc32, expected).map(|_| ())
}
pub fn verification_report(sha256: &str, crc32: &str, expected: &str) -> Result<String> {
    let expected = validate_expected(expected)?;
    let actual = if expected.len() == 64 { sha256 } else { crc32 };
    let algorithm = if expected.len() == 64 {
        "SHA-256"
    } else {
        "CRC32"
    };
    let details = format!("Algorithm: {algorithm}\nActual: {actual}\nExpected: {expected}");
    if !expected.eq_ignore_ascii_case(actual) {
        return Err(ArcError::new(
            "HASH_MISMATCH",
            format!("DOES NOT MATCH\n{details}"),
        ));
    }
    Ok(format!("MATCH\n{details}"))
}
pub fn validate_expected(expected: &str) -> Result<&str> {
    let expected = expected.trim();
    match expected.len() {
        64 | 8 => {}
        _ => {
            return Err(ArcError::new(
                "HASH_FORMAT",
                "Enter 64 SHA-256 or 8 CRC32 hexadecimal characters",
            ));
        }
    }
    if !expected.bytes().all(|b| b.is_ascii_hexdigit()) {
        return Err(ArcError::new(
            "HASH_FORMAT",
            "Digest must contain hexadecimal characters only",
        ));
    }
    Ok(expected)
}
pub fn verify_file(path: &std::path::Path, expected: &str, op: &Operation) -> Result<()> {
    verify_file_report(path, expected, op).map(|_| ())
}
pub fn verify_file_report(
    path: &std::path::Path,
    expected: &str,
    op: &Operation,
) -> Result<String> {
    // Validate before the expensive read, even if the file is missing.
    let expected = validate_expected(expected)?;
    let result = crate::operations::hash_file(path, op)?;
    let (sha, crc) = result
        .split_once('\n')
        .ok_or_else(|| ArcError::new("INTERNAL", "Hash result format"))?;
    verification_report(
        sha.strip_prefix("SHA-256: ").unwrap_or(""),
        crc.strip_prefix("CRC32: ").unwrap_or(""),
        expected,
    )
}
