//! Fixed-size numbered 7Z volumes, read through one open data handle at a time.
use crate::{ArcError, Operation, Result, security};
use std::{
    fs::{self, File},
    io::{self, Read, Seek, SeekFrom, Write},
    path::{Path, PathBuf},
};
const MAX_PARTS: usize = 4096;
pub fn first_part(path: &Path) -> Option<PathBuf> {
    let name = path.file_name()?.to_str()?;
    let (base, number) = name.rsplit_once('.')?;
    if !base.to_ascii_lowercase().ends_with(".7z")
        || number.len() < 3
        || !number.bytes().all(|v| v.is_ascii_digit())
        || number.parse::<u32>().ok()? == 0
    {
        return None;
    }
    Some(path.with_file_name(format!("{base}.001")))
}
fn missing(path: &Path) -> ArcError {
    ArcError::new(
        "MISSING_VOLUME",
        format!("Archive part is missing: {}", path.display()),
    )
}
fn part_path(first: &Path, index: usize) -> PathBuf {
    let base = first
        .file_name()
        .unwrap()
        .to_str()
        .unwrap()
        .strip_suffix(".001")
        .unwrap();
    first.with_file_name(format!("{base}.{index:03}"))
}
struct Part {
    path: PathBuf,
    start: u64,
    length: u64,
}
pub struct Reader {
    parts: Vec<Part>,
    current: Option<(usize, File)>,
    position: u64,
    length: u64,
}
impl Reader {
    pub fn open(path: &Path) -> Result<Self> {
        let first = first_part(path).unwrap_or_else(|| path.to_owned());
        let mut file = File::open(&first).map_err(|e| {
            if e.kind() == io::ErrorKind::NotFound && first_part(path).is_some() {
                missing(&first)
            } else {
                e.into()
            }
        })?;
        let first_length = file.metadata()?.len();
        let mut parts = vec![Part {
            path: first.clone(),
            start: 0,
            length: first_length,
        }];
        let mut length = first_length;
        if first_part(path).is_some() {
            let mut header = [0u8; 32];
            file.read_exact(&mut header).map_err(|_| {
                ArcError::new(
                    "CORRUPT",
                    "First 7Z volume does not contain a complete signature header",
                )
            })?;
            if !header.starts_with(b"7z\xbc\xaf\x27\x1c") {
                return Err(ArcError::new("FORMAT", "Numbered volume is not 7Z"));
            }
            let offset = u64::from_le_bytes(header[12..20].try_into().unwrap());
            let size = u64::from_le_bytes(header[20..28].try_into().unwrap());
            let needed = 32u64
                .checked_add(offset)
                .and_then(|n| n.checked_add(size))
                .ok_or_else(|| ArcError::new("LIMIT", "7Z volume size overflow"))?;
            let count = needed.div_ceil(first_length);
            if count > MAX_PARTS as u64 {
                return Err(ArcError::new("LIMIT", "Too many archive volumes"));
            }
            for index in 2..=count as usize {
                let next = part_path(&first, index);
                let meta = fs::metadata(&next).map_err(|e| {
                    if e.kind() == io::ErrorKind::NotFound {
                        missing(&next)
                    } else {
                        e.into()
                    }
                })?;
                let size = meta.len();
                if !meta.is_file()
                    || size == 0
                    || size > first_length
                    || index < count as usize && size != first_length
                {
                    return Err(ArcError::new(
                        "CORRUPT",
                        format!("Invalid volume size: {}", next.display()),
                    ));
                }
                parts.push(Part {
                    path: next,
                    start: length,
                    length: size,
                });
                length = length
                    .checked_add(size)
                    .ok_or_else(|| ArcError::new("LIMIT", "Volume size overflow"))?;
            }
            if length < needed {
                return Err(ArcError::new("CORRUPT", "Final 7Z volume is truncated"));
            }
        }
        file.seek(SeekFrom::Start(0))?;
        Ok(Self {
            parts,
            current: Some((0, file)),
            position: 0,
            length,
        })
    }
    pub fn len(&self) -> u64 {
        self.length
    }
    pub fn is_empty(&self) -> bool {
        self.length == 0
    }
    pub fn path(&self) -> &Path {
        &self.parts[0].path
    }
}
impl Read for Reader {
    fn read(&mut self, buffer: &mut [u8]) -> io::Result<usize> {
        if buffer.is_empty() || self.position >= self.length {
            return Ok(0);
        }
        let index = self
            .parts
            .partition_point(|p| p.start + p.length <= self.position);
        let part = &self.parts[index];
        if self.current.as_ref().map(|(i, _)| *i) != Some(index) {
            let file = File::open(&part.path).map_err(|e| {
                if e.kind() == io::ErrorKind::NotFound {
                    io::Error::other(missing(&part.path))
                } else {
                    e
                }
            })?;
            if file.metadata()?.len() != part.length {
                return Err(io::Error::other("Archive volume changed since opening"));
            }
            self.current = Some((index, file));
        }
        let file = &mut self.current.as_mut().unwrap().1;
        file.seek(SeekFrom::Start(self.position - part.start))?;
        let wanted = buffer
            .len()
            .min((part.start + part.length - self.position) as usize);
        let count = file.read(&mut buffer[..wanted])?;
        if count == 0 {
            return Err(io::Error::new(
                io::ErrorKind::UnexpectedEof,
                "Archive volume was truncated",
            ));
        }
        self.position += count as u64;
        Ok(count)
    }
}
impl Seek for Reader {
    fn seek(&mut self, from: SeekFrom) -> io::Result<u64> {
        let position = match from {
            SeekFrom::Start(p) => p as i128,
            SeekFrom::End(p) => self.length as i128 + p as i128,
            SeekFrom::Current(p) => self.position as i128 + p as i128,
        };
        self.position = u64::try_from(position)
            .map_err(|_| io::Error::new(io::ErrorKind::InvalidInput, "Invalid volume seek"))?;
        Ok(self.position)
    }
}
/// Publish newly created parts without replacing any existing user file.
/// Multiple names cannot commit atomically; failure attempts cleanup of owned parts.
pub fn publish(source: &mut File, prefix: &Path, size: u64, op: &Operation) -> Result<()> {
    if size < 64 * 1024 {
        return Err(ArcError::new(
            "OPTIONS",
            "Split volume must be at least 64 KiB",
        ));
    }
    let length = source.metadata()?.len();
    let count = length.div_ceil(size);
    if count == 0 || count > MAX_PARTS as u64 {
        return Err(ArcError::new("LIMIT", "Too many split volumes"));
    }
    let first = PathBuf::from(format!("{}.001", prefix.display()));
    let names: Vec<_> = (1..=count as usize).map(|i| part_path(&first, i)).collect();
    for path in &names {
        security::check_ancestors(path)?;
        if path.exists() {
            return Err(ArcError::new(
                "CONFLICT",
                format!("Archive volume already exists: {}", path.display()),
            ));
        }
    }
    let parent = prefix
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or(Path::new("."));
    let _pins = security::pin_ancestors(parent)?;
    source.seek(SeekFrom::Start(0))?;
    let mut staged = Vec::new();
    let mut copied = 0u64;
    op.phase("Staging split volumes");
    for _ in &names {
        op.check()?;
        let mut part = tempfile::NamedTempFile::new_in(parent)?;
        let mut remaining = size;
        let mut buffer = [0u8; 128 * 1024];
        while remaining > 0 {
            op.check()?;
            let wanted = buffer.len().min(remaining as usize);
            let count = source.read(&mut buffer[..wanted])?;
            if count == 0 {
                break;
            }
            part.write_all(&buffer[..count])?;
            copied += count as u64;
            remaining -= count as u64;
        }
        part.as_file().sync_all()?;
        staged.push(part);
    }
    if copied != length {
        return Err(ArcError::new(
            "CHANGED",
            "Archive changed while staging split volumes",
        ));
    }
    let mut published = Vec::new();
    let result = (|| {
        op.phase("Publishing split volumes");
        for (part, name) in staged.into_iter().zip(names) {
            op.check()?;
            security::check_ancestors(&name)?;
            let file = part
                .persist_noclobber(&name)
                .map_err(|e| ArcError::from(e.error))?;
            published.push((name, file));
        }
        Ok(())
    })();
    if result.is_err() {
        for (path, file) in published {
            if let Err(e) = remove_owned(&file, &path) {
                return Err(ArcError::new(
                    "CLEANUP",
                    format!(
                        "Split creation failed; cannot remove newly published part {}: {e}",
                        path.display()
                    ),
                ));
            }
        }
    }
    result
}
#[cfg(windows)]
fn remove_owned(file: &File, _: &Path) -> io::Result<()> {
    use std::os::windows::io::AsRawHandle;
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn ReOpenFile(
            original: *mut std::ffi::c_void,
            access: u32,
            share: u32,
            flags: u32,
        ) -> *mut std::ffi::c_void;
        fn SetFileInformationByHandle(
            file: *mut std::ffi::c_void,
            class: u32,
            data: *const i32,
            size: u32,
        ) -> i32;
        fn CloseHandle(file: *mut std::ffi::c_void) -> i32;
    }
    // SAFETY: reopen operates on our live published file handle, not a pathname
    // another process can replace. The new handle is owned and closed exactly once.
    let handle = unsafe { ReOpenFile(file.as_raw_handle(), 0x10000, 7, 0) };
    if handle as isize == -1 {
        return Err(io::Error::last_os_error());
    }
    let delete = 1i32;
    // SAFETY: FileDispositionInfo is a four-byte BOOL; only this newly owned file
    // object is marked for removal after handles close, never a replacement path.
    let ok = unsafe { SetFileInformationByHandle(handle, 4, &delete, 4) };
    let result = if ok == 0 {
        Err(io::Error::last_os_error())
    } else {
        Ok(())
    };
    unsafe {
        CloseHandle(handle);
    }
    result
}
#[cfg(not(windows))]
fn remove_owned(_: &File, path: &Path) -> io::Result<()> {
    fs::remove_file(path)
}
