use crate::{
    ArcError, Archive, Entry, Format, Result,
    security::{self, Policy},
};
use sha2::{Digest, Sha256};
use std::{
    fs::{self, File},
    io::{Read, Write},
    path::{Path, PathBuf},
    sync::{
        Arc, Condvar, Mutex,
        atomic::{AtomicBool, AtomicU64, Ordering},
    },
};
use zeroize::Zeroizing;

#[derive(Clone, Default)]
pub struct Operation(Arc<State>);
#[derive(Default)]
struct State {
    cancelled: AtomicBool,
    done: AtomicU64,
    total: AtomicU64,
    current: Mutex<String>,
    conflict: Mutex<PendingConflict>,
    conflict_ready: Condvar,
}
#[derive(Clone, Debug)]
pub struct ConflictDetails {
    pub incoming_name: String,
    pub incoming_size: u64,
    pub incoming_modified: Option<u64>,
    pub existing_path: String,
    pub existing_size: u64,
    pub existing_modified: Option<u64>,
}
#[derive(Default)]
struct PendingConflict {
    id: u64,
    request: Option<ConflictDetails>,
    reply: Option<(Conflict, bool)>,
}
#[derive(Clone, Debug)]
pub struct Progress {
    pub done: u64,
    pub total: u64,
    pub current: String,
}
impl Operation {
    pub fn cancel(&self) {
        self.0.cancelled.store(true, Ordering::Relaxed);
        self.0.conflict_ready.notify_all();
    }
    pub fn check(&self) -> Result<()> {
        if self.0.cancelled.load(Ordering::Relaxed) {
            Err(ArcError::new(
                "CANCELLED",
                "Operation cancelled; completed files remain",
            ))
        } else {
            Ok(())
        }
    }
    pub fn phase(&self, current: &str) {
        if let Ok(mut s) = self.0.current.lock() {
            *s = current.to_owned();
        }
    }
    pub fn total(&self, total: u64) {
        self.0.total.store(total, Ordering::Relaxed);
        self.0.done.store(0, Ordering::Relaxed);
    }
    pub fn snapshot(&self) -> Progress {
        Progress {
            done: self.0.done.load(Ordering::Relaxed),
            total: self.0.total.load(Ordering::Relaxed),
            current: self.0.current.lock().map(|v| v.clone()).unwrap_or_default(),
        }
    }
    pub fn advance(&self, n: u64) {
        self.0.done.fetch_add(n, Ordering::Relaxed);
    }
    pub fn pending_conflict(&self) -> Option<(u64, ConflictDetails)> {
        let state = self.0.conflict.lock().ok()?;
        Some((state.id, state.request.clone()?))
    }
    pub fn reply_conflict(&self, id: u64, choice: Conflict, all: bool) -> bool {
        if !matches!(
            choice,
            Conflict::Refuse | Conflict::Replace | Conflict::Skip | Conflict::Rename
        ) {
            return false;
        }
        let Ok(mut state) = self.0.conflict.lock() else {
            return false;
        };
        if state.id != id || state.request.is_none() || state.reply.is_some() {
            return false;
        }
        state.reply = Some((choice, all));
        self.0.conflict_ready.notify_all();
        true
    }
    fn ask_conflict(&self, details: ConflictDetails) -> Result<(Conflict, bool)> {
        self.check()?;
        let internal = |_| ArcError::new("INTERNAL", "Conflict request lock failed");
        let mut state = self.0.conflict.lock().map_err(internal)?;
        state.id += 1;
        state.request = Some(details);
        state.reply = None;
        loop {
            if let Err(error) = self.check() {
                state.request = None;
                return Err(error);
            }
            if let Some(reply) = state.reply.take() {
                state.request = None;
                return Ok(reply);
            }
            state = self
                .0
                .conflict_ready
                .wait_timeout(state, std::time::Duration::from_millis(100))
                .map_err(|_| ArcError::new("INTERNAL", "Conflict wait failed"))?
                .0;
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Conflict {
    Refuse,
    Skip,
    Replace,
    Ask,
    Rename,
    Newer,
}
pub struct ExtractOptions {
    pub destination: PathBuf,
    pub selected: Vec<u64>,
    pub conflict: Conflict,
    pub smart: bool,
    pub policy: Policy,
}
pub struct CreateOptions {
    pub output: PathBuf,
    pub inputs: Vec<PathBuf>,
    pub password: Zeroizing<String>,
}

/// Bounded streaming with limits enforced against actual bytes, not just metadata.
pub(crate) fn copy_checked(
    source: &mut dyn Read,
    target: &mut dyn Write,
    entry: &Entry,
    op: &Operation,
    policy: &Policy,
    actual: &mut u64,
) -> Result<()> {
    let mut buffer = [0u8; 128 * 1024];
    let mut written = 0u64;
    loop {
        op.check()?;
        let n = source.read(&mut buffer)?;
        if n == 0 {
            break;
        }
        written = written
            .checked_add(n as u64)
            .ok_or_else(|| ArcError::new("LIMIT", "Size overflow"))?;
        *actual = actual
            .checked_add(n as u64)
            .ok_or_else(|| ArcError::new("LIMIT", "Total size overflow"))?;
        if written > entry.size
            || written > policy.max_file_bytes
            || *actual > policy.max_total_bytes
        {
            return Err(ArcError::new(
                "LIMIT",
                "Decoded output exceeds safety limit or declared size",
            ));
        }
        target.write_all(&buffer[..n])?;
        op.advance(n as u64);
    }
    if written != entry.size {
        return Err(ArcError::new(
            "CORRUPT",
            format!("Decoded size mismatch: {}", entry.name),
        ));
    }
    Ok(())
}

pub fn extract(
    archive: &Archive,
    options: &ExtractOptions,
    password: &str,
    op: &Operation,
) -> Result<PathBuf> {
    op.check()?;
    let total = archive.validate(&options.policy)?;
    if options.selected.len() > options.policy.max_entries {
        return Err(ArcError::new("LIMIT", "Too many selected entries"));
    }
    let ids: std::collections::HashSet<_> = options.selected.iter().copied().collect();
    let available: std::collections::HashSet<_> = archive.entries.iter().map(|e| e.id).collect();
    let selected = |e: &Entry| ids.is_empty() || ids.contains(&e.id);
    for id in &options.selected {
        if !available.contains(id) {
            return Err(ArcError::new("SELECTION", "Unknown entry id"));
        }
    }
    let stem = archive
        .path
        .file_stem()
        .and_then(|v| v.to_str())
        .unwrap_or("Archive");
    let root = if options.smart {
        security::smart_destination(&options.destination, stem, &archive.entries)?
    } else {
        options.destination.clone()
    };
    security::check_ancestors(&root)?;
    crate::disk::preflight(
        &root,
        archive
            .entries
            .iter()
            .filter(|e| selected(e))
            .map(|e| e.size)
            .sum(),
    )?;
    let _root_pins = security::pin_ancestors(&root)?;
    fs::create_dir_all(&root)?;
    let root = root.canonicalize()?;
    let _created_root_pins = security::pin_ancestors(&root)?;
    // Preflight all conflicts before writing any archive files.
    for entry in archive.entries.iter().filter(|e| selected(e)) {
        let output = security::output_path(&root, &entry.name)?;
        if output.exists() && !entry.directory && options.conflict == Conflict::Refuse {
            return Err(ArcError::new(
                "CONFLICT",
                format!("File already exists: {}", output.display()),
            ));
        }
        if output.exists() && output.is_dir() != entry.directory {
            return Err(ArcError::new(
                "CONFLICT",
                "File and directory types conflict",
            ));
        }
    }
    op.total(total);
    for entry in archive
        .entries
        .iter()
        .filter(|e| e.directory && selected(e))
    {
        op.check()?;
        fs::create_dir_all(security::output_path(&root, &entry.name)?)?;
    }
    let mut actual = 0u64;
    let mut effective = options.conflict;
    let reserved: std::collections::HashSet<_> = archive
        .entries
        .iter()
        .map(|e| security::validate_name(&e.name).map(|n| n.to_uppercase()))
        .collect::<Result<_>>()?;
    archive.streams(password, op, |entry, data| {
        op.phase(&entry.name);
        let mut output = security::output_path(&root, &entry.name)?;
        let mut choice = effective;
        if selected(entry) && output.exists() {
            let existing = fs::metadata(&output)?;
            let modified = existing
                .modified()
                .ok()
                .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
                .map(|d| d.as_secs());
            if choice == Conflict::Newer {
                choice = match (entry.modified_unix, modified) {
                    (Some(incoming), Some(old)) if incoming > old.saturating_add(2) => {
                        Conflict::Replace
                    }
                    (Some(_), Some(_)) => Conflict::Skip,
                    _ => Conflict::Ask,
                };
            }
            if choice == Conflict::Ask {
                let (decision, all) = op.ask_conflict(ConflictDetails {
                    incoming_name: entry.name.clone(),
                    incoming_size: entry.size,
                    incoming_modified: entry.modified_unix,
                    existing_path: output.to_string_lossy().into_owned(),
                    existing_size: existing.len(),
                    existing_modified: modified,
                })?;
                choice = decision;
                if all {
                    effective = decision;
                }
                if choice == Conflict::Refuse {
                    op.cancel();
                    return op.check();
                }
            }
            if choice == Conflict::Rename {
                output = keep_both(&root, &output, &reserved, op)?;
            }
        }
        if !selected(entry) || output.exists() && choice == Conflict::Skip {
            return copy_checked(
                data,
                &mut std::io::sink(),
                entry,
                op,
                &options.policy,
                &mut actual,
            );
        }
        let parent = output
            .parent()
            .ok_or_else(|| ArcError::new("PATH", "No output parent"))?;
        fs::create_dir_all(parent)?;
        let _parent_pins = security::pin_ancestors(parent)?;
        security::check_ancestors(parent)?;
        let mut temp = tempfile::NamedTempFile::new_in(parent)?;
        copy_checked(
            data,
            temp.as_file_mut(),
            entry,
            op,
            &options.policy,
            &mut actual,
        )?;
        temp.as_file().sync_all()?;
        op.check()?;
        security::check_ancestors(&output)?;
        #[cfg(windows)]
        propagate_zone(&archive.path, temp.path())?;
        if let Some(seconds) = entry.modified_unix {
            let modified = std::time::UNIX_EPOCH
                .checked_add(std::time::Duration::from_secs(seconds))
                .ok_or_else(|| ArcError::new("TIME", "Invalid modified time"))?;
            temp.as_file()
                .set_times(fs::FileTimes::new().set_modified(modified))?;
            temp.as_file().sync_all()?;
        }
        op.check()?;
        if choice == Conflict::Replace {
            temp.persist(&output).map_err(|e| ArcError::from(e.error))?;
        } else {
            temp.persist_noclobber(&output)
                .map_err(|e| ArcError::from(e.error))?;
        }
        Ok(())
    })?;
    Ok(root)
}

fn keep_both(
    root: &Path,
    output: &Path,
    reserved: &std::collections::HashSet<String>,
    op: &Operation,
) -> Result<PathBuf> {
    let parent = output
        .parent()
        .ok_or_else(|| ArcError::new("PATH", "No output parent"))?;
    let stem = output
        .file_stem()
        .and_then(|v| v.to_str())
        .ok_or_else(|| ArcError::new("NAME", "Invalid conflict filename"))?;
    let extension = output
        .extension()
        .and_then(|v| v.to_str())
        .map(|e| format!(".{e}"))
        .unwrap_or_default();
    for number in 2..=10000 {
        op.check()?;
        let suffix = format!(" ({number}){extension}");
        let mut base = stem.to_owned();
        while base.encode_utf16().count() + suffix.encode_utf16().count() > 255 {
            if base.pop().is_none() {
                return Err(ArcError::new(
                    "NAME",
                    "Cannot form a safe conflict filename",
                ));
            }
        }
        let path = parent.join(format!("{base}{suffix}"));
        let relative = path
            .strip_prefix(root)
            .map_err(|_| ArcError::new("PATH", "Conflict path escaped destination"))?;
        let name = security::validate_name(&relative.to_string_lossy())?;
        if !path.exists() && !reserved.contains(&name.to_uppercase()) {
            return Ok(path);
        }
    }
    Err(ArcError::new(
        "CONFLICT",
        "Too many existing copies; choose another destination",
    ))
}

#[cfg(windows)]
pub(crate) fn propagate_zone(source: &Path, output: &Path) -> Result<()> {
    let source_ads = format!("{}:Zone.Identifier", source.display());
    match File::open(&source_ads) {
        Ok(source) => {
            if source.metadata()?.len() > 64 * 1024 {
                return Err(ArcError::new(
                    "MOTW",
                    "Source security zone is unexpectedly large",
                ));
            }
            let mut zone = Vec::new();
            source.take(64 * 1024 + 1).read_to_end(&mut zone)?;
            if zone.len() > 64 * 1024 {
                return Err(ArcError::new(
                    "MOTW",
                    "Source security zone grew beyond the limit",
                ));
            }
            fs::write(format!("{}:Zone.Identifier", output.display()), zone)?;
        }
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => {}
        Err(e) => return Err(e.into()),
    }
    Ok(())
}

pub fn test(archive: &Archive, password: &str, op: &Operation) -> Result<()> {
    let policy = Policy::default();
    op.total(archive.validate(&policy)?);
    let mut actual = 0u64;
    archive.streams(password, op, |entry, data| {
        op.phase(&entry.name);
        copy_checked(data, &mut std::io::sink(), entry, op, &policy, &mut actual)
    })
}

pub(crate) fn collect(
    path: &Path,
    name: String,
    entries: &mut Vec<(Entry, PathBuf)>,
    op: &Operation,
) -> Result<()> {
    op.check()?;
    security::check_ancestors(path)?;
    let meta = fs::symlink_metadata(path)?;
    if !meta.is_file() && !meta.is_dir() {
        return Err(ArcError::new(
            "INPUT",
            "Only regular files and directories can be archived",
        ));
    }
    let name = security::validate_name(&name)?;
    if entries.len() >= Policy::default().max_entries {
        return Err(ArcError::new("LIMIT", "Too many input files"));
    }
    entries.push((
        Entry {
            id: entries.len() as u64,
            name: name.clone(),
            size: if meta.is_file() { meta.len() } else { 0 },
            directory: meta.is_dir(),
            ..Default::default()
        },
        path.to_owned(),
    ));
    if meta.is_dir() {
        let mut children = fs::read_dir(path)?.collect::<std::io::Result<Vec<_>>>()?;
        children.sort_by_key(|v| v.file_name());
        for child in children {
            let child_name = child
                .file_name()
                .to_str()
                .ok_or_else(|| ArcError::new("NAME", "Non-Unicode input name"))?
                .to_owned();
            collect(&child.path(), format!("{name}/{child_name}"), entries, op)?;
        }
    }
    Ok(())
}

struct CountReader<'a> {
    file: File,
    op: &'a Operation,
}
impl Read for CountReader<'_> {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        self.op.check().map_err(std::io::Error::other)?;
        let n = self.file.read(buf)?;
        self.op.advance(n as u64);
        Ok(n)
    }
}

pub fn create(options: &CreateOptions, op: &Operation) -> Result<()> {
    create_as(options, Format::for_output(&options.output)?, op)
}

pub fn create_as(options: &CreateOptions, format: Format, op: &Operation) -> Result<()> {
    create_internal(options, format, None, op)
}
pub fn create_split(options: &CreateOptions, size: u64, op: &Operation) -> Result<()> {
    create_internal(options, Format::SevenZ, Some(size), op)
}
fn create_internal(
    options: &CreateOptions,
    format: Format,
    split: Option<u64>,
    op: &Operation,
) -> Result<()> {
    if let Some(size) = split {
        if size < 64 * 1024 {
            return Err(ArcError::new(
                "OPTIONS",
                "Split volume must be at least 64 KiB",
            ));
        }
        let first = PathBuf::from(format!("{}.001", options.output.display()));
        security::check_ancestors(&first)?;
        if first.exists() {
            return Err(ArcError::new(
                "CONFLICT",
                "First archive volume already exists",
            ));
        }
    }
    if options.inputs.is_empty() {
        return Err(ArcError::new(
            "INPUT",
            "Select at least one source file or directory",
        ));
    }
    op.check()?;
    if Format::for_output(&options.output)? != format {
        return Err(ArcError::new(
            "FORMAT",
            "Output extension must match the selected format",
        ));
    }
    if !options.password.is_empty() && matches!(format, Format::Tar | Format::TarGz) {
        return Err(ArcError::new("ENCRYPTION", "TAR cannot be encrypted"));
    }
    security::check_ancestors(&options.output)?;
    if options.output.exists() {
        return Err(ArcError::new(
            "CONFLICT",
            "The output archive already exists",
        ));
    }
    op.phase("Scanning input files");
    let mut sources = Vec::new();
    for input in &options.inputs {
        security::check_ancestors(input)?;
        let path = input.canonicalize()?;
        let name = path
            .file_name()
            .and_then(|s| s.to_str())
            .ok_or_else(|| ArcError::new("NAME", "Input has no valid filename"))?;
        collect(&path, name.into(), &mut sources, op)?;
    }
    let entries: Vec<_> = sources.iter().map(|(e, _)| e.clone()).collect();
    let policy = Policy {
        max_ratio: u64::MAX,
        ..Default::default()
    };
    op.total(security::validate_plan(&entries, 1, &policy)?);
    let required = sources.iter().map(|(e, _)| e.size).sum::<u64>();
    let required = required
        .checked_mul(if split.is_some() { 2 } else { 1 })
        .ok_or_else(|| ArcError::new("LIMIT", "Split space estimate overflow"))?;
    crate::disk::preflight(&options.output, required)?;
    let parent = options
        .output
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or(Path::new("."));
    fs::create_dir_all(parent)?;
    let _output_pins = security::pin_ancestors(parent)?;
    let mut temp = tempfile::NamedTempFile::new_in(parent)?;
    match format {
        #[cfg(windows)]
        Format::Rar => return Err(ArcError::new("READ_ONLY", "RAR creation is unsupported")),
        Format::Zip => {
            let mut writer = zip::ZipWriter::new(temp.as_file_mut());
            let base = zip::write::SimpleFileOptions::default()
                .compression_method(zip::CompressionMethod::Deflated);
            let file_options = if options.password.is_empty() {
                base
            } else {
                base.with_aes_encryption(zip::AesMode::Aes256, &options.password)
            };
            for (entry, path) in &sources {
                op.check()?;
                op.phase(&entry.name);
                if entry.directory {
                    writer.add_directory(format!("{}/", entry.name), base)?;
                } else {
                    writer.start_file(&entry.name, file_options)?;
                    let mut reader = CountReader {
                        file: File::open(path)?,
                        op,
                    };
                    let n = std::io::copy(&mut reader, &mut writer)?;
                    if n != entry.size {
                        return Err(ArcError::new(
                            "CHANGED",
                            "Input size changed during creation",
                        ));
                    }
                }
            }
            writer.finish()?;
        }
        Format::SevenZ => {
            let mut writer = sevenz_rust2::ArchiveWriter::new(temp.as_file_mut())?;
            if !options.password.is_empty() {
                use sevenz_rust2::*;
                writer.set_content_methods(vec![
                    EncoderConfiguration::new(EncoderMethod::AES256_SHA256).with_options(
                        encoder_options::AesEncoderOptions::new(options.password.as_str().into())
                            .into(),
                    ),
                    EncoderConfiguration::new(EncoderMethod::LZMA2),
                ]);
            }
            for (entry, path) in &sources {
                op.check()?;
                op.phase(&entry.name);
                let mut e = sevenz_rust2::ArchiveEntry::from_path(path, entry.name.clone());
                // Explicit DOS attributes ensure other 7Z readers distinguish
                // directory entries from empty files without guessing.
                e.has_windows_attributes = true;
                e.windows_attributes = if entry.directory { 0x10 } else { 0x20 };
                let reader = if entry.directory {
                    None
                } else {
                    Some(CountReader {
                        file: File::open(path)?,
                        op,
                    })
                };
                writer.push_archive_entry(e, reader)?;
            }
            writer.finish()?;
        }
        Format::Tar | Format::TarGz => {
            if format == Format::TarGz {
                let mut gzip = flate2::write::GzEncoder::new(
                    temp.as_file_mut(),
                    flate2::Compression::default(),
                );
                write_tar(&mut gzip, &sources, op)?;
                gzip.finish()?;
            } else {
                write_tar(temp.as_file_mut(), &sources, op)?;
            }
        }
    }
    temp.as_file().sync_all()?;
    op.check()?;
    op.phase("Verifying completed archive");
    let archive = Archive::open(temp.path(), &options.password, op)?;
    test(&archive, &options.password, op)?;
    op.check()?;
    security::check_ancestors(&options.output)?;
    if let Some(size) = split {
        return crate::volumes::publish(temp.as_file_mut(), &options.output, size, op);
    }
    temp.persist_noclobber(&options.output)
        .map_err(|e| ArcError::from(e.error))?;
    Ok(())
}

fn write_tar(target: &mut dyn Write, sources: &[(Entry, PathBuf)], op: &Operation) -> Result<()> {
    let mut writer = tar::Builder::new(target);
    for (entry, path) in sources {
        op.check()?;
        op.phase(&entry.name);
        let mut header = tar::Header::new_gnu();
        header.set_metadata(&fs::metadata(path)?);
        header.set_entry_type(if entry.directory {
            tar::EntryType::Directory
        } else {
            tar::EntryType::Regular
        });
        header.set_size(entry.size);
        header.set_cksum();
        if entry.directory {
            writer.append_data(&mut header, &entry.name, std::io::empty())?;
        } else {
            writer.append_data(
                &mut header,
                &entry.name,
                CountReader {
                    file: File::open(path)?,
                    op,
                },
            )?;
        }
    }
    writer.finish()?;
    Ok(())
}

pub fn hash_file(path: &Path, op: &Operation) -> Result<String> {
    let mut file = File::open(path)?;
    op.total(file.metadata()?.len());
    op.phase("SHA-256 and CRC32");
    let mut sha = Sha256::new();
    let mut crc = crc32fast::Hasher::new();
    let mut buffer = [0u8; 128 * 1024];
    loop {
        op.check()?;
        let n = file.read(&mut buffer)?;
        if n == 0 {
            break;
        }
        sha.update(&buffer[..n]);
        crc.update(&buffer[..n]);
        op.advance(n as u64);
    }
    let hex: String = sha.finalize().iter().map(|v| format!("{v:02x}")).collect();
    Ok(format!("SHA-256: {hex}\nCRC32: {:08x}", crc.finalize()))
}
