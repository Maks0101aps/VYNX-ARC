//! Stream verified content into a new ZIP/7Z; never replace the source.
use crate::{
    ArcError, Archive, Entry, Format, Operation, Result, modification, operations,
    security::{self, Policy},
};
use sha2::{Digest, Sha256};
use std::{
    collections::HashMap,
    fs::File,
    io::{self, Read},
    path::PathBuf,
};
use zeroize::Zeroizing;
pub struct Replacement {
    pub name: String,
    pub input: PathBuf,
    pub archive_hash: String,
    pub input_hash: String,
}
pub struct Options {
    pub output: PathBuf,
    pub format: Format,
    pub password: Zeroizing<String>,
    pub allow_decrypt: bool,
    pub replacement: Option<Replacement>,
}
struct HashReader<'a> {
    data: &'a mut dyn Read,
    sha: Sha256,
}
impl Read for HashReader<'_> {
    fn read(&mut self, bytes: &mut [u8]) -> io::Result<usize> {
        let n = self.data.read(bytes)?;
        self.sha.update(&bytes[..n]);
        Ok(n)
    }
}
pub fn convert(a: &Archive, o: &Options, password: &str, op: &Operation) -> Result<String> {
    op.check()?;
    if !matches!(o.format, Format::Zip | Format::SevenZ)
        || Format::for_output(&o.output)? != o.format
    {
        return Err(ArcError::new(
            "FORMAT",
            "Conversion output must be ZIP or 7Z with a matching extension",
        ));
    }
    if o.password.is_empty()
        && !o.allow_decrypt
        && (!password.is_empty() || a.entries.iter().any(|e| e.encrypted))
    {
        return Err(ArcError::new(
            "ENCRYPTION",
            "Explicitly confirm unencrypted output or supply a destination password",
        ));
    }
    security::check_ancestors(&a.path)?;
    security::check_ancestors(&o.output)?;
    if o.output.exists() {
        return Err(ArcError::new(
            "CONFLICT",
            "Conversion never replaces an existing destination or source",
        ));
    }
    let policy = Policy::default();
    a.validate(&policy)?;
    let _source = modification::lock_source(&a.path)?;
    let mut replacement_guard = None;
    if let Some(r) = &o.replacement {
        security::validate_name(&r.name)?;
        security::check_ancestors(&r.input)?;
        replacement_guard = Some(modification::lock_source(&r.input)?);
        if !r.input.is_file() || r.input.metadata()?.len() > crate::preview::MAX_EXTERNAL_BYTES {
            return Err(ArcError::new(
                "LIMIT",
                "Working copy exceeds 256 MiB or is not a regular file",
            ));
        }
        if operations::hash_file(&a.path, op)? != r.archive_hash {
            return Err(ArcError::new(
                "CHANGED",
                "Archive container changed. Recheck its source before Save as; working copy retained",
            ));
        }
        if operations::hash_file(&r.input, op)? != r.input_hash {
            return Err(ArcError::new(
                "CHANGED",
                "Working file changed during Save as. Finish saving in the editor and retry; copy retained",
            ));
        }
        if !a.entries.iter().any(|e| e.name == r.name && !e.directory) {
            return Err(ArcError::new("SELECTION", "Working-copy entry is missing"));
        }
    }
    let mut planned: HashMap<u64, Entry> = HashMap::new();
    for e in &a.entries {
        let mut entry = e.clone();
        entry.name = security::validate_name(&entry.name)?;
        if let Some(r) = &o.replacement
            && entry.name == r.name
        {
            entry.size = r.input.metadata()?.len();
        }
        planned.insert(e.id, entry);
    }
    let mut entries: Vec<_> = planned.values().cloned().collect();
    entries.sort_by_key(|e| e.id);
    let write_policy = Policy {
        max_ratio: u64::MAX,
        ..policy.clone()
    };
    let total = security::validate_plan(&entries, 1, &write_policy)?;
    let estimate = total
        .checked_add((entries.len() as u64).saturating_mul(4096))
        .ok_or_else(|| ArcError::new("LIMIT", "Conversion space estimate overflow"))?;
    crate::disk::preflight(&o.output, estimate)?;
    let parent = o
        .output
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or(std::path::Path::new("."));
    std::fs::create_dir_all(parent)?;
    let _pins = security::pin_ancestors(parent)?;
    let mut temp = tempfile::NamedTempFile::new_in(parent)?;
    let _priority = crate::priority::WorkerPriority::enter(op.settings().resource);
    op.details(op.settings().effective(o.format)?.details());
    op.total(total);
    op.stage("Converting and verifying source streams");
    let mut writer = modification::Sink::new(temp.as_file_mut(), o.format, &o.password, op)?;
    for entry in entries.iter().filter(|e| e.directory) {
        let (date, seven) = metadata(entry);
        writer.add(
            entry,
            &mut io::empty(),
            &o.password,
            date,
            Some(seven),
            op,
            &write_policy,
        )?;
    }
    let mut wanted = HashMap::new();
    a.streams(password, op, |old, data| {
        op.check()?;
        op.phase(&old.name);
        let entry = planned
            .get(&old.id)
            .ok_or_else(|| ArcError::new("CHANGED", "Source manifest changed during conversion"))?;
        let (date, seven) = metadata(entry);
        let mut input_file;
        let source: &mut dyn Read = if let Some(r) = &o.replacement
            && entry.name == r.name
        {
            operations::copy_checked(data, &mut io::sink(), old, op, &policy, &mut 0)?;
            input_file = File::open(&r.input)?;
            &mut input_file
        } else {
            data
        };
        let mut reader = HashReader {
            data: source,
            sha: Sha256::new(),
        };
        writer.add(
            entry,
            &mut reader,
            &o.password,
            date,
            Some(seven),
            op,
            &write_policy,
        )?;
        if !entry.directory {
            let sha: String = reader
                .sha
                .finalize()
                .iter()
                .map(|v| format!("{v:02x}"))
                .collect();
            wanted.insert(entry.name.clone(), sha);
        }
        Ok(())
    })?;
    writer.finish()?;
    temp.as_file().sync_all()?;
    op.check()?;
    op.stage("Independently decoding converted output");
    let verified = Archive::open(temp.path(), &o.password, op)?;
    let expected: std::collections::HashSet<_> = entries
        .iter()
        .map(|e| (e.name.clone(), e.size, e.directory))
        .collect();
    let actual: std::collections::HashSet<_> = verified
        .entries
        .iter()
        .map(|e| Ok((security::validate_name(&e.name)?, e.size, e.directory)))
        .collect::<Result<_>>()?;
    if expected != actual {
        return Err(ArcError::new(
            "VERIFY",
            "Converted manifest differs; source and destination preserved",
        ));
    }
    let hashes = crate::hashing::entries(&verified, &[], &o.password, op)?;
    if hashes.len() != wanted.len()
        || hashes
            .iter()
            .any(|h| wanted.get(&h.name) != Some(&h.sha256))
    {
        return Err(ArcError::new(
            "VERIFY",
            "Converted file hashes differ; archive not published",
        ));
    }
    drop(verified);
    drop(replacement_guard);
    #[cfg(windows)]
    operations::propagate_zone(&a.path, temp.path())?;
    temp.as_file().sync_all()?;
    let receipt = operations::hash_file(temp.path(), op)?;
    op.prepare_publication(&[(o.output.clone(), temp.path().to_owned())])?;
    op.check()?;
    security::check_ancestors(&o.output)?;
    op.stage("Publishing verified conversion");
    temp.persist_noclobber(security::publication_path(&o.output)?)
        .map_err(|e| ArcError::from(e.error))?;
    Ok(receipt)
}

fn metadata(entry: &Entry) -> (Option<zip::DateTime>, sevenz_rust2::ArchiveEntry) {
    let mut seven = sevenz_rust2::ArchiveEntry::default();
    if let Some(seconds) = entry
        .modified_unix
        .and_then(|n| std::time::UNIX_EPOCH.checked_add(std::time::Duration::from_secs(n)))
        && let Ok(date) = sevenz_rust2::NtTime::try_from(seconds)
    {
        seven.last_modified_date = date;
        seven.has_last_modified_date = true;
    }
    let date = entry.modified_unix.and_then(crate::timestamps::unix_zip);
    (date, seven)
}
