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
    archive.validate(&policy)?;
    security::check_ancestors(&archive.path)?;
    if archive.entries.iter().any(|e| e.encrypted) && password.is_empty() {
        return Err(ArcError::new(
            "PASSWORD",
            "Supply the archive password; encryption will be preserved",
        ));
    }
    let source_guard = lock_source(&archive.path)?;
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
    op.total(total);
    let mut replacement = tempfile::NamedTempFile::new_in(parent.canonicalize()?)?;
    diagnostic("REBUILD_STARTED", &archive.path, Some(replacement.path()));
    let mut zip_metadata = if archive.format == Format::Zip {
        Some(zip::ZipArchive::new(File::open(&archive.path)?)?)
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
    let mut writer = Sink::new(replacement.as_file_mut(), archive.format, password)?;
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
            writer.add(new, data, password, date, seven, op, &write_policy)?;
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
    drop(source_guard);
    diagnostic(
        "SOURCE_GUARD_DROPPED",
        &archive.path,
        Some(replacement.path()),
    );
    // No cancellation after the commit decision: a successful publish is reported
    // as success so the GUI refreshes instead of claiming the old archive survived.
    commit(replacement, &archive.path, op)
}

fn commit(replacement: tempfile::NamedTempFile, path: &Path, op: &Operation) -> Result<()> {
    op.check()?;
    let publication_path = security::publication_path(path)?;
    diagnostic("PUBLISH_BEGIN", path, Some(replacement.path()));
    match replacement.persist(publication_path) {
        Ok(_) => {
            diagnostic("PUBLISH_SUCCESS", path, None);
            Ok(())
        }
        Err(error) => {
            diagnostic("PUBLISH_FAIL", path, Some(error.file.path()));
            Err(ArcError::new(
                "REPLACE",
                format!(
                    "Could not replace archive {} (OS error {:?}): {}; original preserved",
                    path.display(),
                    error.error.raw_os_error(),
                    error.error,
                ),
            ))
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

fn lock_source(path: &Path) -> Result<File> {
    let mut options = OpenOptions::new();
    options.read(true);
    #[cfg(windows)]
    {
        use std::os::windows::fs::OpenOptionsExt;
        options.share_mode(1); // Readers permitted; modification/deletion denied.
    }
    Ok(options.open(path)?)
}

enum Sink<'a> {
    Zip(Box<zip::ZipWriter<&'a mut File>>),
    Seven(sevenz_rust2::ArchiveWriter<&'a mut File>),
}
impl<'a> Sink<'a> {
    fn new(file: &'a mut File, format: Format, password: &str) -> Result<Self> {
        if format == Format::Zip {
            return Ok(Self::Zip(Box::new(zip::ZipWriter::new(file))));
        }
        let mut writer = sevenz_rust2::ArchiveWriter::new(file)?;
        if !password.is_empty() {
            use sevenz_rust2::*;
            writer.set_content_methods(vec![
                EncoderConfiguration::new(EncoderMethod::AES256_SHA256)
                    .with_options(encoder_options::AesEncoderOptions::new(password.into()).into()),
                EncoderConfiguration::new(EncoderMethod::LZMA2),
            ]);
        }
        Ok(Self::Seven(writer))
    }
    #[allow(clippy::too_many_arguments)]
    fn add(
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
                    .compression_method(zip::CompressionMethod::Deflated);
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
    fn finish(self) -> Result<()> {
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
        assert_eq!(error.code, "REPLACE");
        assert!(error.message.contains("OS error Some(5)"));
        assert_eq!(std::fs::read(path).unwrap(), b"original");
        assert!(!temp.exists());
    }
}
