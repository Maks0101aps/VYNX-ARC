use crate::{
    ArcError, Result,
    operations::Operation,
    security::{Policy, validate_plan},
};
use std::{
    fs::File,
    io::{Read, Seek, SeekFrom},
    path::{Path, PathBuf},
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Format {
    Zip,
    SevenZ,
    Tar,
    TarGz,
    #[cfg(windows)]
    Rar,
}
impl Format {
    pub fn name(self) -> &'static str {
        match self {
            Self::Zip => "ZIP",
            Self::SevenZ => "7Z",
            Self::Tar => "TAR",
            Self::TarGz => "TAR.GZ",
            #[cfg(windows)]
            Self::Rar => "RAR",
        }
    }
    pub fn for_output(path: &Path) -> Result<Self> {
        let name = path.to_string_lossy().to_lowercase();
        if name.ends_with(".zip") {
            Ok(Self::Zip)
        } else if name.ends_with(".7z") {
            Ok(Self::SevenZ)
        } else if name.ends_with(".tar.gz") || name.ends_with(".tgz") {
            Ok(Self::TarGz)
        } else if name.ends_with(".tar") {
            Ok(Self::Tar)
        } else {
            Err(ArcError::new("FORMAT", "Choose ZIP, 7Z, TAR, or TAR.GZ"))
        }
    }
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Entry {
    pub id: u64,
    pub name: String,
    pub size: u64,
    pub packed: u64,
    pub directory: bool,
    pub link: bool,
    pub encrypted: bool,
    pub crc: Option<u32>,
    pub modified: String,
    pub modified_unix: Option<u64>,
}

#[derive(Clone)]
pub struct Archive {
    pub path: PathBuf,
    pub format: Format,
    pub entries: Vec<Entry>,
    pub physical_size: u64,
}

pub fn detect(file: &mut File) -> Result<Format> {
    let mut header = [0u8; 512];
    let n = file.read(&mut header)?;
    file.seek(SeekFrom::Start(0))?;
    if header[..n].starts_with(b"PK\x03\x04") || header[..n].starts_with(b"PK\x05\x06") {
        return Ok(Format::Zip);
    }
    if header[..n].starts_with(b"7z\xbc\xaf\x27\x1c") {
        return Ok(Format::SevenZ);
    }
    #[cfg(windows)]
    if header[..n].starts_with(b"Rar!\x1a\x07") {
        return Ok(Format::Rar);
    }
    if header[..n].starts_with(&[0x1f, 0x8b]) {
        return Ok(Format::TarGz);
    }
    if n >= 512 && (&header[257..262] == b"ustar" || header.iter().all(|x| *x == 0)) {
        return Ok(Format::Tar);
    }
    Err(ArcError::new(
        "FORMAT",
        "Unrecognized or currently unsupported archive format",
    ))
}

impl Archive {
    pub fn open(path: &Path, password: &str, op: &Operation) -> Result<Self> {
        op.phase("Reading archive metadata");
        let mut file = File::open(path)?;
        let physical_size = file.metadata()?.len();
        let format = detect(&mut file)?;
        let mut entries = Vec::new();
        let max_entries = Policy::default().max_entries;
        match format {
            #[cfg(windows)]
            Format::Rar => {
                entries = crate::rar::list(path, password, op)?;
            }
            Format::Zip => {
                let mut zip = zip::ZipArchive::new(file)?;
                if zip.len() > max_entries {
                    return Err(ArcError::new("LIMIT", "Too many entries"));
                }
                for i in 0..zip.len() {
                    op.check()?;
                    let f = zip.by_index_raw(i)?;
                    entries.push(Entry {
                        id: i as u64,
                        name: f.name().to_owned(),
                        size: f.size(),
                        packed: f.compressed_size(),
                        directory: f.is_dir(),
                        link: f.is_symlink(),
                        encrypted: f.encrypted(),
                        crc: Some(f.crc32()),
                        modified: f.last_modified().map(|d| d.to_string()).unwrap_or_default(),
                        modified_unix: f.last_modified().and_then(crate::timestamps::zip_unix),
                    });
                }
            }
            Format::SevenZ => {
                let reader = sevenz_rust2::ArchiveReader::new(file, password.into())?;
                if reader.archive().files.len() > max_entries {
                    return Err(ArcError::new("LIMIT", "Too many entries"));
                }
                for (i, f) in reader.archive().files.iter().enumerate() {
                    op.check()?;
                    entries.push(Entry {
                        id: i as u64,
                        name: f.name.clone(),
                        size: f.size,
                        packed: f.compressed_size,
                        directory: f.is_directory,
                        link: f.is_anti_item
                            || f.has_windows_attributes
                                && (f.windows_attributes & 0x400 != 0
                                    || ((f.windows_attributes >> 16) & 0xf000) == 0xa000),
                        encrypted: reader.archive().blocks.iter().any(|b| {
                            b.coders
                                .iter()
                                .any(|c| c.encoder_method_id() == [0x06, 0xf1, 0x07, 0x01])
                        }),
                        crc: f.has_crc.then_some(f.crc as u32),
                        modified: String::new(),
                        modified_unix: f
                            .has_last_modified_date
                            .then(|| crate::timestamps::nt_unix(f.last_modified_date.into()))
                            .flatten(),
                    });
                }
            }
            Format::Tar | Format::TarGz => {
                let source: Box<dyn Read> = if format == Format::TarGz {
                    Box::new(flate2::read::GzDecoder::new(file))
                } else {
                    Box::new(file)
                };
                for (i, f) in tar::Archive::new(source).entries()?.enumerate() {
                    op.check()?;
                    if i >= max_entries {
                        return Err(ArcError::new("LIMIT", "Too many entries"));
                    }
                    let f = f?;
                    let kind = f.header().entry_type();
                    let name = f
                        .path()?
                        .to_str()
                        .ok_or_else(|| ArcError::new("NAME", "Non-Unicode entry name"))?
                        .to_owned();
                    entries.push(Entry {
                        id: i as u64,
                        name,
                        size: f.size(),
                        modified_unix: f.header().mtime().ok(),
                        directory: kind.is_dir(),
                        link: !kind.is_file() && !kind.is_dir(),
                        ..Default::default()
                    });
                }
            }
        }
        Ok(Self {
            path: path.to_owned(),
            format,
            entries,
            physical_size,
        })
    }

    pub fn validate(&self, policy: &Policy) -> Result<u64> {
        validate_plan(&self.entries, self.physical_size, policy)
    }

    /// Decode data through a caller-controlled bounded stream consumer; never use backend extraction helpers.
    pub fn streams(
        &self,
        password: &str,
        op: &Operation,
        mut consume: impl FnMut(&Entry, &mut dyn Read) -> Result<()>,
    ) -> Result<()> {
        let file = File::open(&self.path)?;
        match self.format {
            #[cfg(windows)]
            Format::Rar => {
                crate::rar::streams(&self.path, password, &self.entries, op, consume)?;
            }
            Format::Zip => {
                let mut zip = zip::ZipArchive::new(file)?;
                if zip.len() != self.entries.len() {
                    return Err(ArcError::new(
                        "CHANGED",
                        "Archive entry count changed since opening",
                    ));
                }
                for entry in &self.entries {
                    op.check()?;
                    if entry.directory {
                        continue;
                    }
                    let mut f = if password.is_empty() {
                        zip.by_index(entry.id as usize)?
                    } else {
                        zip.by_index_decrypt(entry.id as usize, password.as_bytes())?
                    };
                    if f.name() != entry.name
                        || f.size() != entry.size
                        || entry.crc.is_some_and(|crc| crc != f.crc32())
                    {
                        return Err(ArcError::new("CHANGED", "Archive changed since opening"));
                    }
                    consume(entry, &mut f)?;
                }
            }
            Format::SevenZ => {
                let mut reader = sevenz_rust2::ArchiveReader::new(file, password.into())?;
                if reader.archive().files.len() != self.entries.len() {
                    return Err(ArcError::new(
                        "CHANGED",
                        "Archive entry count changed since opening",
                    ));
                }
                reader.set_thread_count(2);
                let mut callback_error = None;
                let indexed: std::collections::HashMap<_, _> =
                    self.entries.iter().map(|e| (e.name.as_str(), e)).collect();
                reader
                    .for_each_entries(|f, data| {
                        let result = (|| {
                            op.check()?;
                            let entry = indexed.get(f.name.as_str()).ok_or_else(|| {
                                ArcError::new("CHANGED", "Archive entry count changed")
                            })?;
                            if entry.name != f.name || entry.size != f.size {
                                return Err(ArcError::new(
                                    "CHANGED",
                                    "Archive changed since opening",
                                ));
                            }
                            if !entry.directory {
                                consume(entry, data)?;
                            }
                            Ok(())
                        })();
                        match result {
                            Ok(()) => Ok(true),
                            Err(e) => {
                                callback_error = Some(e);
                                Err(std::io::Error::other("Operation stopped").into())
                            }
                        }
                    })
                    .map_err(|e| callback_error.unwrap_or_else(|| e.into()))?;
            }
            Format::Tar | Format::TarGz => {
                let source: Box<dyn Read> = if self.format == Format::TarGz {
                    Box::new(flate2::read::GzDecoder::new(file))
                } else {
                    Box::new(file)
                };
                let mut count = 0usize;
                for (i, f) in tar::Archive::new(source).entries()?.enumerate() {
                    op.check()?;
                    let mut f = f?;
                    let entry = self
                        .entries
                        .get(i)
                        .ok_or_else(|| ArcError::new("CHANGED", "Archive entry count changed"))?;
                    if f.path()?.to_str() != Some(entry.name.as_str()) || f.size() != entry.size {
                        return Err(ArcError::new("CHANGED", "Archive changed since opening"));
                    }
                    if !entry.directory {
                        consume(entry, &mut f)?;
                    }
                    count += 1;
                }
                if count != self.entries.len() {
                    return Err(ArcError::new("CHANGED", "Archive was truncated"));
                }
            }
        }
        Ok(())
    }
}
