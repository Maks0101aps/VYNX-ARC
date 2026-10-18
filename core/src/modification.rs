//! Streaming rebuild: no plaintext extraction workspace and no in-place mutation.
use crate::{
    ArcError, Archive, Entry, Format, Operation, Result, disk, operations,
    security::{self, Policy},
};
use std::{
    collections::{HashMap, HashSet},
    fs::{File, OpenOptions},
    io::Read,
    path::{Path, PathBuf},
};

pub enum Change {
    Replace {
        name: String,
        input: PathBuf,
        expected_archive_hash: String,
        expected_input_hash: String,
    },
    Add {
        inputs: Vec<PathBuf>,
        folder: String,
    },
    Delete {
        names: Vec<String>,
    },
    Rename {
        old: String,
        new: String,
    },
}

fn under(name: &str, prefix: &str) -> bool {
    name == prefix
        || name
            .strip_prefix(prefix)
            .is_some_and(|s| s.starts_with('/'))
}

/// Rebuild ZIP/7Z on the same volume, verify, flush, then atomically replace.
/// The source is held against write/delete while streaming. A final pathname
/// publication still has the documented hostile-local-process race limitation.
pub fn modify(archive: &Archive, change: &Change, password: &str, op: &Operation) -> Result<()> {
    modify_with_receipt(archive, change, password, op).map(|_| ())
}
/// The replacement digest is computed before publication. No cancellable
/// post-commit work may turn a completed save into an apparent failure.
pub fn modify_with_receipt(
    archive: &Archive,
    change: &Change,
    password: &str,
    op: &Operation,
) -> Result<String> {
    op.check()?;
    if !matches!(archive.format, Format::Zip | Format::SevenZ)
        || crate::volumes::first_part(&archive.path).is_some()
    {
        return Err(ArcError::new(
            "READ_ONLY",
            "This format is read only; modification supports ZIP and 7Z",
        ));
    }
    let policy = Policy::default();
    let source_total = archive.validate(&policy)?;
    let _priority = crate::priority::WorkerPriority::enter(op.settings().resource);
    let effective = op.settings().effective(archive.format)?;
    op.details(effective.details());
    security::check_ancestors(&archive.path)?;
    if archive.entries.iter().any(|e| e.encrypted) && password.is_empty() {
        return Err(ArcError::new(
            "PASSWORD",
            "Supply the archive password; encryption will be preserved",
        ));
    }
    let source_guard = lock_source(&archive.path)?;
    let mut edit_guard = None;
    if let Change::Replace {
        input,
        expected_archive_hash,
        expected_input_hash,
        ..
    } = change
    {
        security::check_ancestors(input)?;
        edit_guard = Some(lock_source(input)?);
        if !input.is_file() || input.metadata()?.len() > crate::preview::MAX_EXTERNAL_BYTES {
            return Err(ArcError::new(
                "LIMIT",
                "Edited file exceeds 256 MiB or is not a regular file",
            ));
        }
        if operations::hash_file(&archive.path, op)? != *expected_archive_hash {
            return Err(ArcError::new(
                "CHANGED",
                "Archive container changed since this working copy was prepared. Keep the copy; recheck its source before saving.",
            ));
        }
        if operations::hash_file(input, op)? != *expected_input_hash {
            return Err(ArcError::new(
                "CHANGED",
                "Working file changed while the save was being prepared. Finish saving in the editor and retry; the working copy is retained.",
            ));
        }
    }
    diagnostic("SOURCE_LOCKED", &archive.path, None);
    let parent = archive
        .path
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or(Path::new("."));
    let _pins = security::pin_ancestors(parent)?;
    let mut retained = HashMap::new();
    let mut planned = Vec::new();
    let mut additions = Vec::new();
    let normalized: Vec<_> = archive
        .entries
        .iter()
        .map(|e| security::validate_name(&e.name))
        .collect::<Result<_>>()?;
    let available_names: HashSet<_> = normalized.iter().map(String::as_str).collect();
    let (deletes, rename) = match change {
        Change::Replace { name, .. } => {
            let name = security::validate_name(name)?;
            if !archive
                .entries
                .iter()
                .any(|e| e.name == name && !e.directory)
            {
                return Err(ArcError::new("SELECTION", "Edited entry no longer exists"));
            }
            (vec![], None)
        }
        Change::Delete { names } => {
            if names.is_empty() {
                return Err(ArcError::new("SELECTION", "Select entries to delete"));
            }
            let names = names
                .iter()
                .map(|n| security::validate_name(n))
                .collect::<Result<Vec<_>>>()?;
            for name in &names {
                if !available_names.contains(name.as_str())
                    && !normalized.iter().any(|n| under(n, name))
                {
                    return Err(ArcError::new("SELECTION", "Entry no longer exists"));
                }
            }
            (names, None)
        }
        Change::Rename { old, new } => {
            let old = security::validate_name(old)?;
            let new = security::validate_name(new)?;
            if !available_names.contains(old.as_str()) && !normalized.iter().any(|n| under(n, &old))
            {
                return Err(ArcError::new("SELECTION", "Entry no longer exists"));
            }
            if under(&new, &old) && new != old {
                return Err(ArcError::new("NAME", "Cannot move a folder inside itself"));
            }
            (vec![], Some((old, new)))
        }
        Change::Add { inputs, folder } => {
            if inputs.is_empty() {
                return Err(ArcError::new("INPUT", "Choose files or folders to add"));
            }
            let folder = if folder.is_empty() {
                String::new()
            } else {
                security::validate_name(folder)?
            };
            for input in inputs {
                security::check_ancestors(input)?;
                let path = input.canonicalize()?;
                let leaf = path
                    .file_name()
                    .and_then(|s| s.to_str())
                    .ok_or_else(|| ArcError::new("NAME", "Invalid source filename"))?;
                let name = if folder.is_empty() {
                    leaf.into()
                } else {
                    format!("{folder}/{leaf}")
                };
                operations::collect(&path, name, &mut additions, op)?;
            }
            (vec![], None)
        }
    };
    for (e, name) in archive.entries.iter().zip(normalized) {
        if deletes.iter().any(|p| under(&name, p)) {
            continue;
        }
        let mut entry = e.clone();
        entry.name = if let Some((old, new)) = &rename {
            if under(&name, old) {
                format!("{new}{}", &name[old.len()..])
            } else {
                name
            }
        } else {
            name
        };
        if let Change::Replace { name, input, .. } = change
            && entry.name == *name
        {
            entry.size = input.metadata()?.len();
            entry.crc = None;
        }
        retained.insert(e.id, entry.clone());
        planned.push(entry);
    }
    planned.extend(additions.iter().map(|(e, _)| e.clone()));
    let write_policy = Policy {
        max_ratio: u64::MAX,
        ..policy.clone()
    };
    let total = security::validate_plan(&planned, 1, &write_policy)?;
    let estimate = total
        .checked_mul(2)
        .and_then(|n| n.checked_add(archive.physical_size))
        .ok_or_else(|| ArcError::new("LIMIT", "Replacement estimate overflow"))?;
    disk::preflight(parent, estimate)?;
    op.stage("Rebuilding archive");
    let extra = match change {
        Change::Replace { input, .. } => input.metadata()?.len(),
        _ => additions
            .iter()
            .filter(|(e, _)| !e.directory)
            .map(|(e, _)| e.size)
            .sum(),
    };
    op.total(
        source_total
            .checked_add(extra)
            .ok_or_else(|| ArcError::new("LIMIT", "Rebuild progress overflow"))?,
    );
    let mut replacement = tempfile::NamedTempFile::new_in(parent.canonicalize()?)?;
    diagnostic("REBUILD_STARTED", &archive.path, Some(replacement.path()));
    let mut zip_metadata = if archive.format == Format::Zip {
        Some(crate::parser::open_zip(File::open(&archive.path)?)?)
    } else {
        None
    };
    let seven_metadata = if archive.format == Format::SevenZ {
        let r = sevenz_rust2::ArchiveReader::new(File::open(&archive.path)?, password.into())?;
        Some(
            r.archive()
                .files
                .iter()
                .map(|e| {
                    (
                        security::validate_name(&e.name).unwrap_or_else(|_| e.name.clone()),
                        e.clone(),
                    )
                })
                .collect::<HashMap<_, _>>(),
        )
    } else {
        None
    };
    let mut writer = Sink::new(replacement.as_file_mut(), archive.format, password, op)?;
    if let (Sink::Zip(w), Some(z)) = (&mut writer, &zip_metadata) {
        w.set_raw_comment(z.comment().to_vec().into())?;
    }
    for entry in planned.iter().filter(|e| e.directory) {
        writer.add(
            entry,
            &mut std::io::empty(),
            password,
            None,
            None,
            op,
            &write_policy,
        )?;
    }
    archive.streams(password, op, |old, data| {
        op.phase(&old.name);
        if let Some(new) = retained.get(&old.id) {
            let date = zip_metadata
                .as_mut()
                .and_then(|z| z.by_index_raw(old.id as usize).ok())
                .and_then(|f| f.last_modified());
            let seven = seven_metadata
                .as_ref()
                .and_then(|m| m.get(&security::validate_name(&old.name).ok()?))
                .cloned();
            if let Change::Replace { name, input, .. } = change {
                if old.name == *name {
                    operations::copy_checked(data, &mut std::io::sink(), old, op, &policy, &mut 0)?;
                    writer.add(
                        new,
                        &mut File::open(input)?,
                        password,
                        date,
                        seven,
                        op,
                        &write_policy,
                    )?;
                } else {
                    writer.add(new, data, password, date, seven, op, &write_policy)?;
                }
            } else {
                writer.add(new, data, password, date, seven, op, &write_policy)?;
            }
        } else {
            let mut actual = 0;
            operations::copy_checked(data, &mut std::io::sink(), old, op, &policy, &mut actual)?;
        }
        Ok(())
    })?;
    for (entry, path) in additions.iter().filter(|(e, _)| !e.directory) {
        op.phase(&entry.name);
        writer.add(
            entry,
            &mut File::open(path)?,
            password,
            None,
            None,
            op,
            &write_policy,
        )?;
    }
    writer.finish()?;
    diagnostic("REBUILD_FINISHED", &archive.path, Some(replacement.path()));
    replacement.as_file().sync_all()?;
    diagnostic("TEMP_SYNCED", &archive.path, Some(replacement.path()));
    op.check()?;
    op.phase("Verifying replacement archive");
    diagnostic("VERIFY_OPEN", &archive.path, Some(replacement.path()));
    let verified = Archive::open(replacement.path(), password, op)?;
    operations::test(&verified, password, op)?;
    if let Change::Replace {
        name,
        expected_input_hash,
        ..
    } = change
    {
        let entry = verified
            .entries
            .iter()
            .find(|e| e.name == *name)
            .ok_or_else(|| ArcError::new("VERIFY", "Replaced entry missing"))?;
        let hashes = crate::hashing::entries(&verified, &[entry.id], password, op)?;
        if hashes.first().is_none_or(|h| {
            format!("SHA-256: {}\nCRC32: {}", h.sha256, h.crc32) != *expected_input_hash
        }) {
            return Err(ArcError::new(
                "VERIFY",
                "Replacement content differs; original preserved",
            ));
        }
    }
    diagnostic("VERIFY_FINISHED", &archive.path, Some(replacement.path()));
    let wanted: HashSet<_> = planned
        .iter()
        .map(|e| {
            (
                security::validate_name(&e.name).unwrap(),
                e.size,
                e.directory,
            )
        })
        .collect();
    let got: HashSet<_> = verified
        .entries
        .iter()
        .map(|e| {
            (
                security::validate_name(&e.name).unwrap(),
                e.size,
                e.directory,
            )
        })
        .collect();
    if wanted != got {
        return Err(ArcError::new(
            "VERIFY",
            "Replacement manifest differs; original preserved",
        ));
    }
    drop(verified);
    diagnostic("VERIFY_DROPPED", &archive.path, Some(replacement.path()));
    #[cfg(windows)]
    operations::propagate_zone(&archive.path, replacement.path())?;
    replacement.as_file().sync_all()?;
    op.check()?;
    security::check_ancestors(&archive.path)?;
    diagnostic(
        "FINAL_ANCESTOR_CHECK",
        &archive.path,
        Some(replacement.path()),
    );
    drop(zip_metadata);
    diagnostic(
        "METADATA_HANDLES_DROPPED",
        &archive.path,
        Some(replacement.path()),
    );
    let receipt = if matches!(change, Change::Replace { .. }) {
        operations::hash_file(replacement.path(), op)?
    } else {
        String::new()
    };
    drop(source_guard);
    diagnostic(
        "SOURCE_GUARD_DROPPED",
        &archive.path,
        Some(replacement.path()),
    );
    // No cancellation after the commit decision: a successful publish is reported
    // as success so the GUI refreshes instead of claiming the old archive survived.
    drop(edit_guard);
    op.stage("Publishing verified archive");
    commit(replacement, &archive.path, op)?;
    Ok(receipt)
}

fn commit(replacement: tempfile::NamedTempFile, path: &Path, op: &Operation) -> Result<()> {
    op.check()?;
    let publication_path = security::publication_path(path)?;
    op.prepare_publication(&[(path.to_owned(), replacement.path().to_owned())])?;
    op.check()?;
    diagnostic("PUBLISH_BEGIN", path, Some(replacement.path()));
    match replacement.persist(publication_path) {
        Ok(_) => {
            diagnostic("PUBLISH_SUCCESS", path, None);
            Ok(())
        }
        Err(error) => {
            diagnostic("PUBLISH_FAIL", path, Some(error.file.path()));
            Err(modification_error(error.error, path))
        }
    }
}

fn diagnostic(stage: &str, source: &Path, temp: Option<&Path>) {
    #[cfg(feature = "publication-diagnostics")]
    {
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default();
        eprintln!(
            "PUBLICATION time={}.{:09} pid={} stage={stage} source={source:?} temp={temp:?} parent={:?}",
            now.as_secs(),
            now.subsec_nanos(),
            std::process::id(),
            source.parent()
        );
        if matches!(stage, "PUBLISH_BEGIN" | "PUBLISH_FAIL")
            && !(stage == "PUBLISH_BEGIN"
                && std::env::var_os("VYNX_DIAGNOSTIC_ON_FAILURE_ONLY").is_some())
            && let (Some(python), Some(helper), Some(temp)) = (
                std::env::var_os("VYNX_DIAGNOSTIC_PYTHON"),
                std::env::var_os("VYNX_DIAGNOSTIC_HELPER"),
                temp,
            )
        {
            // Synchronous helper snapshots real kernel handles while this process
            // remains at the publication boundary. No password is transferred.
            let result = std::process::Command::new(python)
                .arg(helper)
                .arg("--pid")
                .arg(std::process::id().to_string())
                .arg("--source")
                .arg(source)
                .arg("--temp")
                .arg(temp)
                .status();
            eprintln!("PUBLICATION helper={result:?}");
        }
    }
    #[cfg(not(feature = "publication-diagnostics"))]
    let _ = (stage, source, temp);
}

pub(crate) fn lock_source(path: &Path) -> Result<File> {
    let mut options = OpenOptions::new();
    options.read(true);
    #[cfg(windows)]
    {
        use std::os::windows::fs::OpenOptionsExt;
        options.share_mode(1); // Readers permitted; modification/deletion denied.
    }
    options.open(path).map_err(|e| modification_error(e, path))
}

fn modification_error(error: std::io::Error, path: &Path) -> ArcError {
    let (code, advice) = match error.raw_os_error() {
        Some(32 | 33) => (
            "ARCHIVE_BUSY",
            "Another application is using or locking this archive. Close that application, reload the archive, and run the operation again.",
        ),
        Some(5) => (
            "ACCESS_DENIED",
            "Access was denied. Check permissions and the read-only attribute; another application may also prevent replacement. Reload the archive before trying again.",
        ),
        _ => (
            "REPLACE",
            "Check available disk space and access to the archive, then reload it before trying again.",
        ),
    };
    ArcError::new(
        code,
        format!(
            "Could not update {} (OS error {:?}): {}. The original archive was not replaced. {advice}",
            path.display(),
            error.raw_os_error(),
            error
        ),
    )
}

pub(crate) enum Sink<'a> {
    Zip(Box<zip::ZipWriter<&'a mut File>>),
    Seven(sevenz_rust2::ArchiveWriter<&'a mut File>),
}
impl<'a> Sink<'a> {
    pub(crate) fn new(
        file: &'a mut File,
        format: Format,
        password: &str,
        op: &Operation,
    ) -> Result<Self> {
        if format == Format::Zip {
            return Ok(Self::Zip(Box::new(zip::ZipWriter::new(file))));
        }
        let mut writer = sevenz_rust2::ArchiveWriter::new(file)?;
        writer.set_content_methods(operations::seven_methods(
            &op.settings().effective(format)?,
            password,
        ));
        Ok(Self::Seven(writer))
    }
    #[allow(clippy::too_many_arguments)]
    pub(crate) fn add(
        &mut self,
        entry: &Entry,
        data: &mut dyn Read,
        password: &str,
        date: Option<zip::DateTime>,
        metadata: Option<sevenz_rust2::ArchiveEntry>,
        op: &Operation,
        policy: &Policy,
    ) -> Result<()> {
        op.check()?;
        match self {
            Self::Zip(writer) => {
                let mut base = zip::write::SimpleFileOptions::default()
                    .compression_method(if op.settings().preset == crate::settings::Preset::Store {
                        zip::CompressionMethod::Stored
                    } else {
                        zip::CompressionMethod::Deflated
                    })
                    .compression_level(
                        (op.settings().preset != crate::settings::Preset::Store)
                            .then_some(op.settings().effective(Format::Zip)?.level as i64),
                    );
                if let Some(date) = date {
                    base = base.last_modified_time(date);
                }
                if entry.directory {
                    writer.add_directory(format!("{}/", entry.name), base)?;
                } else {
                    let options = if password.is_empty() {
                        base
                    } else {
                        base.with_aes_encryption(zip::AesMode::Aes256, password)
                    };
                    writer.start_file(&entry.name, options)?;
                    operations::copy_checked(data, writer, entry, op, policy, &mut 0)?;
                }
            }
            Self::Seven(writer) => {
                let mut e = metadata.unwrap_or_default();
                e.name = entry.name.clone();
                e.is_directory = entry.directory;
                e.has_stream = !entry.directory && entry.size > 0;
                e.has_windows_attributes = true;
                e.windows_attributes = if entry.directory { 0x10 } else { 0x20 };
                if entry.directory {
                    writer.push_archive_entry(e, None::<&mut dyn Read>)?;
                } else {
                    let mut reader = CheckedReader {
                        data,
                        op,
                        expected: entry.size,
                        done: 0,
                    };
                    writer.push_archive_entry(e, Some(&mut reader))?;
                    if reader.done != entry.size {
                        return Err(ArcError::new(
                            "CORRUPT",
                            "Decoded size changed during rebuild",
                        ));
                    }
                }
            }
        }
        Ok(())
    }
    pub(crate) fn finish(self) -> Result<()> {
        match self {
            Self::Zip(w) => {
                w.finish()?;
            }
            Self::Seven(w) => {
                w.finish()?;
            }
        };
        Ok(())
    }
}
struct CheckedReader<'a> {
    data: &'a mut dyn Read,
    op: &'a Operation,
    expected: u64,
    done: u64,
}
impl Read for CheckedReader<'_> {
    fn read(&mut self, buffer: &mut [u8]) -> std::io::Result<usize> {
        self.op.check().map_err(std::io::Error::other)?;
        let n = self.data.read(buffer)?;
        self.done = self
            .done
            .checked_add(n as u64)
            .ok_or_else(|| std::io::Error::other("Size overflow"))?;
        if self.done > self.expected || (n == 0 && self.done != self.expected) {
            return Err(std::io::Error::other("Decoded size mismatch"));
        }
        self.op.advance(n as u64);
        Ok(n)
    }
}

#[cfg(test)]
mod publication_tests {
    use super::*;
    use std::io::Write;

    fn staged(parent: &Path) -> tempfile::NamedTempFile {
        let mut file = tempfile::NamedTempFile::new_in(parent).unwrap();
        file.write_all(b"replacement").unwrap();
        file.as_file().sync_all().unwrap();
        file
    }

    #[test]
    fn persist_replaces_flushed_sibling_and_removes_temporary_name() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("archive.zip");
        std::fs::write(&path, b"original").unwrap();
        let replacement = staged(dir.path());
        let temp = replacement.path().to_owned();
        commit(replacement, &path, &Operation::default()).unwrap();
        assert_eq!(std::fs::read(path).unwrap(), b"replacement");
        assert!(!temp.exists());
    }

    #[test]
    fn cancelled_commit_preserves_original_and_cleans_staging() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("archive.zip");
        std::fs::write(&path, b"original").unwrap();
        let replacement = staged(dir.path());
        let temp = replacement.path().to_owned();
        let op = Operation::default();
        op.cancel();
        assert_eq!(
            commit(replacement, &path, &op).unwrap_err().code,
            "CANCELLED"
        );
        assert_eq!(std::fs::read(path).unwrap(), b"original");
        assert!(!temp.exists());
    }

    #[cfg(windows)]
    #[test]
    fn read_only_commit_reports_first_failure_and_preserves_original() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("archive.zip");
        std::fs::write(&path, b"original").unwrap();
        let original_permissions = std::fs::metadata(&path).unwrap().permissions();
        let mut read_only = original_permissions.clone();
        read_only.set_readonly(true);
        std::fs::set_permissions(&path, read_only).unwrap();
        let replacement = staged(dir.path());
        let temp = replacement.path().to_owned();
        let result = commit(replacement, &path, &Operation::default());
        std::fs::set_permissions(&path, original_permissions).unwrap();
        let error = result.unwrap_err();
        assert_eq!(error.code, "ACCESS_DENIED");
        assert!(error.message.contains("OS error Some(5)"));
        assert_eq!(std::fs::read(path).unwrap(), b"original");
        assert!(!temp.exists());
    }
}
