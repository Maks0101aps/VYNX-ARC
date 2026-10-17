//! Read-only UTF-8 preview. No temporary plaintext file or external handler.
use crate::{ArcError, Archive, Operation, Result, operations::copy_checked, security::Policy};

pub const MAX_PREVIEW_BYTES: u64 = 1 << 20;
pub const MAX_CONTENT_BYTES: u64 = 16 << 20;
pub const MAX_EXTERNAL_BYTES: u64 = 256 << 20;

/// Return verified bounded bytes, without publishing any plaintext on disk.
pub fn bytes(archive: &Archive, id: u64, password: &str, op: &Operation) -> Result<Vec<u8>> {
    let mut bytes = Vec::new();
    read_into(archive, id, password, op, MAX_CONTENT_BYTES, &mut bytes)?;
    Ok(bytes)
}

fn read_into(
    archive: &Archive,
    id: u64,
    password: &str,
    op: &Operation,
    limit: u64,
    target: &mut dyn std::io::Write,
) -> Result<()> {
    op.check()?;
    let policy = Policy::default();
    archive.validate(&policy)?;
    let selected = archive
        .entries
        .iter()
        .find(|e| e.id == id && !e.directory)
        .ok_or_else(|| ArcError::new("SELECTION", "Select one regular file"))?;
    if selected.size > limit {
        return Err(ArcError::new(
            "PREVIEW",
            "File exceeds the viewer limit. Extract it instead.",
        ));
    }
    op.stage("Reading verified content");
    if archive.format == crate::Format::Zip {
        op.total(selected.size);
        return archive.zip_entry(id, password, op, |entry, data| {
            copy_checked(data, target, entry, op, &policy, &mut 0)
        });
    }
    op.total(archive.entries.iter().map(|e| e.size).sum());
    let mut actual = 0;
    let mut found = false;
    archive.streams(password, op, |entry, data| {
        if entry.id == id {
            found = true;
            copy_checked(data, target, entry, op, &policy, &mut actual)?;
        } else {
            copy_checked(data, &mut std::io::sink(), entry, op, &policy, &mut actual)?;
        }
        Ok(())
    })?;
    op.check()?;
    if !found {
        return Err(ArcError::new("CHANGED", "Selected entry no longer exists"));
    }
    Ok(())
}

/// Materialize one verified file in a caller-owned private directory. No execution.
/// Staging is removed on decode, integrity or cancellation errors.
pub fn materialize(
    archive: &Archive,
    id: u64,
    destination: &std::path::Path,
    password: &str,
    op: &Operation,
) -> Result<std::path::PathBuf> {
    use crate::security;
    op.check()?;
    archive.validate(&Policy::default())?;
    let entry = archive
        .entries
        .iter()
        .find(|e| e.id == id && !e.directory)
        .ok_or_else(|| ArcError::new("SELECTION", "Select one regular file"))?;
    let name = security::validate_name(&entry.name)?;
    let leaf = name.rsplit('/').next().expect("validated name");
    if entry.size > MAX_EXTERNAL_BYTES {
        return Err(ArcError::new(
            "PREVIEW",
            "External opening is limited to 256 MiB. Extract this file instead.",
        ));
    }
    security::check_ancestors(destination)?;
    let _pins = security::pin_ancestors(destination)?;
    let root = destination.canonicalize()?;
    let output = security::output_path(&root, leaf)?;
    crate::disk::preflight(&root, entry.size)?;
    let mut staged = tempfile::NamedTempFile::new_in(&root)?;
    read_into(
        archive,
        id,
        password,
        op,
        MAX_EXTERNAL_BYTES,
        staged.as_file_mut(),
    )?;
    staged.as_file_mut().sync_all()?;
    // Treat an external view as downloaded content even if the container has no MOTW.
    #[cfg(windows)]
    std::fs::write(
        format!("{}:Zone.Identifier", staged.path().display()),
        b"[ZoneTransfer]\r\nZoneId=3\r\n",
    )?;
    op.check()?;
    let file = staged.persist_noclobber(&output).map_err(|e| e.error)?;
    let mut permissions = file.metadata()?.permissions();
    permissions.set_readonly(true);
    file.set_permissions(permissions)?;
    Ok(output)
}

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
