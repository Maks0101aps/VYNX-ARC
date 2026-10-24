//! Independent RAR5 encoding. UnRAR is used only by the existing read/verify path.
use crate::{ArcError, Entry, Format, Operation, Result, security};
use rars::rar50::{self, ArchiveEntry, ArchiveExtras, WriterOptions};
use rars::{
    ArchiveVersion, EntrySource, FeatureSet, WriteProgress, WriteProgressEvent, WriterResources,
};
use std::{
    fs::{self, File},
    io::{Read, Seek, SeekFrom, Write},
    path::{Path, PathBuf},
};
use zeroize::Zeroize;

#[derive(Clone, Copy, Debug, Default)]
pub struct Options {
    pub solid: bool,
    /// Zero selects the resource/preset dictionary. Otherwise a power of two.
    pub dictionary: u64,
    pub encrypt_headers: bool,
    /// Zero disables recovery generation; otherwise 1..=10 percent.
    pub recovery_percent: u8,
}
impl Options {
    pub fn validate(self, op: &Operation) -> Result<()> {
        let cap = match op.settings().resource {
            crate::settings::ResourceMode::Eco => 2 << 20,
            crate::settings::ResourceMode::Balanced => 8 << 20,
            crate::settings::ResourceMode::Maximum => 32 << 20,
        };
        if self.dictionary != 0
            && (self.dictionary < 128 << 10
                || !self.dictionary.is_power_of_two()
                || self.dictionary > cap)
        {
            return Err(ArcError::new(
                "OPTIONS",
                "RAR dictionary must be a power of two from 128 KiB up to the selected resource limit (2/8/32 MiB)",
            ));
        }
        if self.recovery_percent > 10 {
            return Err(ArcError::new(
                "OPTIONS",
                "RAR recovery records require 1..10 percent, or zero to disable",
            ));
        }
        Ok(())
    }
}

fn error(e: rars::Error) -> ArcError {
    use rars::ErrorKind as K;
    let code = match e.kind() {
        K::Cancelled => "CANCELLED",
        K::ResourceLimit => "LIMIT",
        K::SourceChanged => "CHANGED",
        K::InvalidArgument | K::UnsupportedFeature => "OPTIONS",
        K::UnsafePath => "UNSAFE_PATH",
        K::Io => "IO",
        _ => "RAR_WRITE",
    };
    ArcError::new(code, format!("RAR5 encoder: {e}"))
}
struct Reporter<'a>(&'a Operation);
impl WriteProgress for Reporter<'_> {
    fn is_cancelled(&self) -> bool {
        self.0.check().is_err()
    }
    fn report(&self, event: WriteProgressEvent<'_>) {
        use rars::WriteOperation as W;
        match event {
            WriteProgressEvent::EntryStarted { name, .. } => {
                self.0.phase(&String::from_utf8_lossy(name))
            }
            WriteProgressEvent::OperationStarted {
                operation,
                total_bytes,
                ..
            } => {
                self.0.stage(match operation {
                    W::Compression => "Compressing RAR5 files",
                    W::Recovery => "Generating RAR5 recovery records",
                    W::Emission => "Writing RAR5 archive",
                    _ => "Preparing RAR5 archive",
                });
                self.0.total(total_bytes.unwrap_or(0));
            }
            WriteProgressEvent::Advanced {
                completed_bytes,
                total_bytes,
                ..
            } => {
                self.0.total(total_bytes);
                self.0.advance(completed_bytes);
            }
            WriteProgressEvent::BytesWritten { completed_bytes } => {
                self.0.total(0);
                self.0.advance(completed_bytes);
            }
            _ => {}
        }
    }
}

// Reader remains locked against writes/deletes while the encoder consumes it.
struct Input {
    file: File,
    expected: u64,
    done: u64,
    op: Operation,
}
impl Read for Input {
    fn read(&mut self, bytes: &mut [u8]) -> std::io::Result<usize> {
        self.op.check().map_err(std::io::Error::other)?;
        let n = self.file.read(bytes)?;
        self.done = self
            .done
            .checked_add(n as u64)
            .ok_or_else(|| std::io::Error::other("Input size overflow"))?;
        if self.done > self.expected || (n == 0 && self.done != self.expected) {
            return Err(std::io::Error::other("Input changed during RAR creation"));
        }
        Ok(n)
    }
}
impl Seek for Input {
    fn seek(&mut self, pos: SeekFrom) -> std::io::Result<u64> {
        self.done = self.file.seek(pos)?;
        Ok(self.done)
    }
}
fn entries(
    sources: &[(Entry, PathBuf)],
    password: &str,
    op: &Operation,
) -> Result<Vec<ArchiveEntry>> {
    sources
        .iter()
        .map(|(entry, path)| {
            op.check()?;
            security::validate_name(&entry.name)?;
            security::check_ancestors(path)?;
            let metadata = fs::metadata(path)?;
            let modified = metadata.modified()?;
            let expected = entry.size;
            let path = path.clone();
            let operation = op.clone();
            let source = if entry.directory {
                EntrySource::from_bytes(Vec::new())
            } else {
                EntrySource::from_opener(expected, move || {
                    security::check_ancestors(&path).map_err(std::io::Error::other)?;
                    let file =
                        crate::modification::lock_source(&path).map_err(std::io::Error::other)?;
                    let metadata = file.metadata()?;
                    if metadata.len() != expected
                        || metadata.modified()? != modified
                        || !metadata.is_file()
                    {
                        return Err(rars::Error::SourceChanged(
                            "Input changed before RAR creation",
                        ));
                    }
                    Ok(Box::new(Input {
                        file,
                        expected,
                        done: 0,
                        op: operation.clone(),
                    }))
                })
            };
            let mut out = ArchiveEntry::new(entry.name.as_bytes(), source)
                .with_directory(entry.directory)
                .with_attributes(if entry.directory { 0x10 } else { 0x20 })
                .with_mtime(
                    metadata
                        .modified()
                        .ok()
                        .and_then(|v| v.duration_since(std::time::UNIX_EPOCH).ok())
                        .and_then(|v| u32::try_from(v.as_secs()).ok()),
                );
            if !password.is_empty() {
                out = out.with_password(password.as_bytes());
            }
            Ok(out)
        })
        .collect()
}
struct SecretEntries(Vec<ArchiveEntry>);
impl Drop for SecretEntries {
    fn drop(&mut self) {
        for entry in &mut self.0 {
            if let Some(password) = &mut entry.password {
                password.zeroize();
            }
        }
    }
}
fn configuration(op: &Operation) -> Result<(WriterOptions, WriterResources)> {
    let config = op.rar_options();
    config.validate(op)?;
    let mut effective = op.settings().effective(Format::Rar)?;
    if config.dictionary != 0 {
        effective.dictionary = config.dictionary as u32;
    }
    op.details(format!(
        "{}; solid {}; encrypted headers {}; recovery {}%",
        effective.details(),
        config.solid,
        config.encrypt_headers,
        config.recovery_percent
    ));
    let mut features = FeatureSet::store_only();
    features.solid = config.solid;
    features.header_encryption = config.encrypt_headers;
    let options = WriterOptions::new(ArchiveVersion::Rar50, features)
        .with_compression_level(effective.level as u8)
        .with_dictionary_size(if config.dictionary == 0 {
            effective.dictionary as u64
        } else {
            config.dictionary
        });
    let resources = WriterResources::new(effective.encoder_budget)
        .with_max_memory_bytes(effective.encoder_budget)
        .with_max_prepared_header_bytes(32 << 20)
        .with_max_preparation_bytes(64 << 20)
        .with_max_spool_bytes(security::Policy::default().max_total_bytes);
    Ok((options, resources))
}
fn extras<'a>(password: &'a str, op: &Operation) -> ArchiveExtras<'a> {
    let mut extras = ArchiveExtras::default().with_recovery_percent(
        (op.rar_options().recovery_percent > 0).then_some(op.rar_options().recovery_percent as u64),
    );
    if !password.is_empty() {
        extras.header_password = Some(password.as_bytes());
    }
    extras
}
pub(crate) fn write(
    output: &mut File,
    sources: &[(Entry, PathBuf)],
    password: &str,
    op: &Operation,
) -> Result<()> {
    let (options, resources) = configuration(op)?;
    if options.features.header_encryption && password.is_empty() {
        return Err(ArcError::new(
            "PASSWORD",
            "RAR header encryption requires a password",
        ));
    }
    let scratch = op.rar_scratch()?;
    crate::disk::preflight(scratch.path(), sources.iter().map(|(e, _)| e.size).sum())?;
    let resources = resources.with_temp_dir(scratch.path());
    let entries = SecretEntries(entries(sources, password, op)?);
    let result = rar50::write_streaming_archive_with_progress(
        &entries.0,
        options,
        extras(password, op),
        &resources,
        Some(&Reporter(op)),
        output,
    );
    op.check()?;
    result.map_err(error)
}

struct VolumeFiles {
    root: PathBuf,
    paths: Vec<PathBuf>,
}
impl rar50::VolumeSink for VolumeFiles {
    fn start_volume(&mut self, index: u64) -> rars::Result<Box<dyn Write + Send>> {
        if index as usize != self.paths.len() || index >= 4096 {
            return Err(rars::Error::InvalidArgument("RAR volume count/order limit"));
        }
        let path = self.root.join(format!("archive.part{}.rar", index + 1));
        let file = fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&path)?;
        self.paths.push(path);
        Ok(Box::new(file))
    }
    fn finish_volume(&mut self, index: u64, len: u64) -> rars::Result<()> {
        let file = fs::OpenOptions::new()
            .read(true)
            .write(true)
            .open(&self.paths[index as usize])?;
        if file.metadata()?.len() != len {
            return Err(rars::Error::SourceChanged("RAR volume size changed"));
        }
        file.sync_all()?;
        Ok(())
    }
}
pub(crate) fn create_volumes(
    options: &crate::CreateOptions,
    sources: &[(Entry, PathBuf)],
    size: u64,
    op: &Operation,
) -> Result<()> {
    let (writer, resources) = configuration(op)?;
    if size < 64 << 10 {
        return Err(ArcError::new(
            "OPTIONS",
            "RAR volume payload must be at least 64 KiB",
        ));
    }
    let parent = options
        .output
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or(Path::new("."));
    let scratch = op.rar_scratch()?;
    let resources = resources.with_temp_dir(scratch.path());
    let mut sink = VolumeFiles {
        root: scratch.path().to_owned(),
        paths: Vec::new(),
    };
    let entries = SecretEntries(entries(sources, &options.password, op)?);
    let encoded = rar50::write_streaming_volumes_with_progress(
        &entries.0,
        writer,
        extras(&options.password, op),
        size,
        &mut sink,
        &resources,
        Some(&Reporter(op)),
    );
    op.check()?;
    encoded.map_err(error)?;
    let first = sink
        .paths
        .first()
        .ok_or_else(|| ArcError::new("VERIFY", "RAR writer produced no volumes"))?;
    op.stage("Verifying RAR5 volume set with independent decoder");
    let archive = crate::Archive::open(first, &options.password, op)?;
    crate::operations::test(&archive, &options.password, op)?;
    verify_manifest(
        &archive,
        &sources.iter().map(|(e, _)| e.clone()).collect::<Vec<_>>(),
    )?;
    let stem = options
        .output
        .file_stem()
        .and_then(|s| s.to_str())
        .ok_or_else(|| ArcError::new("NAME", "Invalid RAR output name"))?;
    // The output is a base name; actual paths are BASE.part1.rar, BASE.part2.rar, ...
    let targets: Vec<_> = (1..=sink.paths.len())
        .map(|n| parent.join(format!("{stem}.part{n}.rar")))
        .collect();
    for path in &targets {
        security::check_ancestors(path)?;
        if path.exists() {
            return Err(ArcError::new(
                "CONFLICT",
                format!("RAR volume exists: {}", path.display()),
            ));
        }
    }
    let mut staged = Vec::new();
    for path in &sink.paths {
        op.check()?;
        let mut temp = tempfile::NamedTempFile::new_in(parent)?;
        let mut input = File::open(path)?;
        let mut buffer = [0; 128 * 1024];
        loop {
            op.check()?;
            let n = input.read(&mut buffer)?;
            if n == 0 {
                break;
            }
            temp.write_all(&buffer[..n])?;
        }
        temp.as_file().sync_all()?;
        staged.push(temp);
    }
    op.stage("Publishing verified RAR5 volumes");
    op.prepare_publication(
        &targets
            .iter()
            .zip(&staged)
            .map(|(p, f)| (p.clone(), f.path().to_owned()))
            .collect::<Vec<_>>(),
    )?;
    let mut published = Vec::new();
    let result = (|| {
        for (temp, path) in staged.into_iter().zip(targets) {
            op.check()?;
            security::check_ancestors(&path)?;
            let file = temp
                .persist_noclobber(security::publication_path(&path)?)
                .map_err(|e| ArcError::from(e.error))?;
            published.push((path, file));
        }
        Ok(())
    })();
    if result.is_err() {
        for (path, file) in published {
            crate::volumes::remove_owned(&file, &path).map_err(|e| {
                ArcError::new(
                    "CLEANUP",
                    format!(
                        "RAR publication failed; cannot remove owned part {}: {e}",
                        path.display()
                    ),
                )
            })?;
        }
    }
    result
}

pub(crate) fn verify_manifest(archive: &crate::Archive, expected: &[Entry]) -> Result<()> {
    use std::collections::HashSet;
    let wanted: HashSet<_> = expected
        .iter()
        .map(|e| security::validate_name(&e.name).map(|name| (name, e.size, e.directory)))
        .collect::<Result<_>>()?;
    let got: HashSet<_> = archive
        .entries
        .iter()
        .map(|e| security::validate_name(&e.name).map(|name| (name, e.size, e.directory)))
        .collect::<Result<_>>()?;
    if got != wanted {
        return Err(ArcError::new(
            "VERIFY",
            "RAR manifest differs from inputs; output was not published",
        ));
    }
    Ok(())
}

/// One anonymous, delete-on-close spool holds all retained plaintext. A reader
/// owns only its byte range and cursor; no per-entry file handles or paths.
struct SpoolReader {
    file: std::sync::Arc<std::sync::Mutex<File>>,
    offset: u64,
    len: u64,
    pos: u64,
    op: Operation,
}
impl Read for SpoolReader {
    fn read(&mut self, bytes: &mut [u8]) -> std::io::Result<usize> {
        self.op.check().map_err(std::io::Error::other)?;
        let count = bytes.len().min(self.len.saturating_sub(self.pos) as usize);
        let mut file = self
            .file
            .lock()
            .map_err(|_| std::io::Error::other("RAR spool lock failed"))?;
        file.seek(SeekFrom::Start(
            self.offset
                .checked_add(self.pos)
                .ok_or_else(|| std::io::Error::other("RAR spool offset overflow"))?,
        ))?;
        let n = file.read(&mut bytes[..count])?;
        self.pos += n as u64;
        Ok(n)
    }
}
impl Seek for SpoolReader {
    fn seek(&mut self, from: SeekFrom) -> std::io::Result<u64> {
        let next = match from {
            SeekFrom::Start(n) => n as i128,
            SeekFrom::Current(n) => self.pos as i128 + n as i128,
            SeekFrom::End(n) => self.len as i128 + n as i128,
        };
        self.pos =
            u64::try_from(next).map_err(|_| std::io::Error::other("Invalid RAR spool seek"))?;
        Ok(self.pos)
    }
}
pub(crate) struct Rebuild<'a> {
    output: &'a mut File,
    spool: std::sync::Arc<std::sync::Mutex<File>>,
    sources: SecretEntries,
    op: Operation,
    password: zeroize::Zeroizing<String>,
}
impl<'a> Rebuild<'a> {
    pub(crate) fn new(output: &'a mut File, password: &str, op: &Operation) -> Result<Self> {
        Ok(Self {
            output,
            spool: std::sync::Arc::new(std::sync::Mutex::new(tempfile::tempfile()?)),
            sources: SecretEntries(Vec::new()),
            op: op.clone(),
            password: password.to_owned().into(),
        })
    }
    pub(crate) fn add(
        &mut self,
        entry: &Entry,
        data: &mut dyn Read,
        policy: &security::Policy,
    ) -> Result<()> {
        security::validate_name(&entry.name)?;
        let source = if entry.directory {
            EntrySource::from_bytes(Vec::new())
        } else {
            let mut file = self
                .spool
                .lock()
                .map_err(|_| ArcError::new("INTERNAL", "RAR spool lock failed"))?;
            let offset = file.seek(SeekFrom::End(0))?;
            crate::operations::copy_checked(data, &mut *file, entry, &self.op, policy, &mut 0)?;
            let spool = self.spool.clone();
            let len = entry.size;
            let op = self.op.clone();
            EntrySource::from_opener(len, move || {
                Ok(Box::new(SpoolReader {
                    file: spool.clone(),
                    offset,
                    len,
                    pos: 0,
                    op: op.clone(),
                }))
            })
        };
        let mut member = ArchiveEntry::new(entry.name.as_bytes(), source)
            .with_directory(entry.directory)
            .with_attributes(if entry.directory { 0x10 } else { 0x20 })
            .with_mtime(entry.modified_unix.and_then(|n| u32::try_from(n).ok()));
        if !self.password.is_empty() {
            member = member.with_password(self.password.as_bytes());
        }
        self.sources.0.push(member);
        Ok(())
    }
    pub(crate) fn finish(self) -> Result<()> {
        let (options, resources) = configuration(&self.op)?;
        let scratch = self.op.rar_scratch()?;
        let resources = resources.with_temp_dir(scratch.path());
        let result = rar50::write_streaming_archive_with_progress(
            &self.sources.0,
            options,
            extras(&self.password, &self.op),
            &resources,
            Some(&Reporter(&self.op)),
            self.output,
        );
        self.op.check()?;
        result.map_err(error)
    }
}
