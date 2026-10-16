//! Read-only UTF-8 preview. No temporary plaintext file or external handler.
use crate::{ArcError, Archive, Operation, Result, operations::copy_checked, security::Policy};

pub const MAX_PREVIEW_BYTES: u64 = 1 << 20;

pub fn text(archive: &Archive, id: u64, password: &str, op: &Operation) -> Result<String> {
    let policy = Policy::default();
    archive.validate(&policy)?;
    let selected = archive
        .entries
        .iter()
        .find(|e| e.id == id && !e.directory)
        .ok_or_else(|| ArcError::new("SELECTION", "Select one regular file"))?;
    if selected.size > MAX_PREVIEW_BYTES {
        return Err(ArcError::new(
            "PREVIEW",
            "Text preview is limited to 1 MiB. Extract this file to view it.",
        ));
    }
    op.total(archive.entries.iter().map(|e| e.size).sum());
    let mut bytes = Vec::new();
    let mut actual = 0;
    let mut found = false;
    archive.streams(password, op, |entry, data| {
        if entry.id == id {
            found = true;
            copy_checked(data, &mut bytes, entry, op, &policy, &mut actual)?;
        } else {
            copy_checked(data, &mut std::io::sink(), entry, op, &policy, &mut actual)?;
        }
        Ok(())
    })?;
    if !found {
        return Err(ArcError::new("CHANGED", "Preview entry no longer exists"));
    }
    let text = String::from_utf8(bytes).map_err(|_| {
        ArcError::new(
            "PREVIEW",
            "This file is not UTF-8 text. Extract it to view it.",
        )
    })?;
    if text
        .chars()
        .any(|c| c.is_control() && !matches!(c, '\n' | '\r' | '\t'))
    {
        return Err(ArcError::new(
            "PREVIEW",
            "This file contains binary data. Extract it to view it.",
        ));
    }
    Ok(text)
}
