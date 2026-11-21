//! Pure safe Rust sector parser for ISO 9660 and Joliet disc images.
use crate::{
    ArcError, Entry, Result,
    operations::Operation,
    security::{Policy, validate_name},
};
use std::{
    collections::{HashSet, VecDeque},
    io::{Read, Seek, SeekFrom},
    path::Path,
};

/// Standard optical disc sector size in bytes.
pub const SECTOR_SIZE: u64 = 2048;

/// Detect if the provided stream contains an ISO 9660 or UDF disc image.
pub fn detect(file: &mut (impl Read + Seek)) -> Result<bool> {
    let initial_pos = file.stream_position()?;
    let length = file.seek(SeekFrom::End(0))?;
    if length < 32768 + SECTOR_SIZE {
        file.seek(SeekFrom::Start(initial_pos))?;
        return Ok(false);
    }

    file.seek(SeekFrom::Start(32768))?;
    let mut header = [0u8; SECTOR_SIZE as usize];

    // Check sector 16 first
    if file.read_exact(&mut header).is_ok() {
        let id = &header[1..6];
        if id == b"CD001" || id == b"BEA01" || id == b"NSR02" || id == b"NSR03" || id == b"TEA01" {
            file.seek(SeekFrom::Start(initial_pos))?;
            return Ok(true);
        }
    }

    // Check subsequent sectors 17..32
    for sector in 17..32 {
        let pos = (sector as u64) * SECTOR_SIZE;
        if pos + SECTOR_SIZE > length {
            break;
        }
        if file.seek(SeekFrom::Start(pos)).is_err() || file.read_exact(&mut header).is_err() {
            break;
        }
        let id = &header[1..6];
        if id == b"CD001" || id == b"BEA01" || id == b"NSR02" || id == b"NSR03" || id == b"TEA01" {
            file.seek(SeekFrom::Start(initial_pos))?;
            return Ok(true);
        }
        if header[0] == 255 && id == b"CD001" {
            break;
        }
    }

    file.seek(SeekFrom::Start(initial_pos))?;
    Ok(false)
}

#[derive(Clone, Copy, Debug)]
struct RootRecord {
    lba: u32,
    len: u32,
}

fn parse_root_record(rec: &[u8]) -> Result<RootRecord> {
    if rec.len() < 34 {
        return Err(ArcError::new(
            "CORRUPT",
            "Root directory record is truncated",
        ));
    }
    let rec_len = rec[0] as usize;
    if rec_len < 34 || rec_len > rec.len() {
        return Err(ArcError::new(
            "CORRUPT",
            "Invalid root directory record length",
        ));
    }
    let lba = u32::from_le_bytes(rec[2..6].try_into().unwrap());
    let len = u32::from_le_bytes(rec[10..14].try_into().unwrap());
    Ok(RootRecord { lba, len })
}

fn is_leap_year(year: u32) -> bool {
    (year.is_multiple_of(4) && !year.is_multiple_of(100)) || year.is_multiple_of(400)
}

fn parse_iso_datetime(bytes: &[u8]) -> (String, Option<u64>) {
    if bytes.len() < 7 {
        return (String::new(), None);
    }
    let year = 1900 + bytes[0] as u32;
    let month = bytes[1] as u32;
    let day = bytes[2] as u32;
    let hour = bytes[3] as u32;
    let minute = bytes[4] as u32;
    let second = bytes[5] as u32;
    let offset_15min = bytes[6] as i8;

    if bytes[0] == 0 && month == 0 && day == 0 {
        return (String::new(), None);
    }
    if !(1..=12).contains(&month)
        || !(1..=31).contains(&day)
        || hour > 23
        || minute > 59
        || second > 59
    {
        return (String::new(), None);
    }

    let date_str = format!(
        "{:04}-{:02}-{:02} {:02}:{:02}:{:02}",
        year, month, day, hour, minute, second
    );

    if !(1970..=2100).contains(&year) {
        return (date_str, None);
    }

    const DAYS_BEFORE_MONTH: [u32; 12] = [0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334];
    let mut days = 0u64;
    for y in 1970..year {
        days += if is_leap_year(y) { 366 } else { 365 };
    }
    days += DAYS_BEFORE_MONTH[(month - 1) as usize] as u64;
    if month > 2 && is_leap_year(year) {
        days += 1;
    }
    days += (day - 1) as u64;

    let total_seconds =
        days * 86400 + (hour as u64) * 3600 + (minute as u64) * 60 + (second as u64);
    let offset_seconds = (offset_15min as i64) * 15 * 60;
    let unix_ts = (total_seconds as i64) - offset_seconds;
    let modified_unix = if unix_ts >= 0 {
        Some(unix_ts as u64)
    } else {
        None
    };

    (date_str, modified_unix)
}

/// List all entries in an ISO 9660 / Joliet image, returning entries and total unpacked size.
pub fn list(
    file: &mut (impl Read + Seek),
    _path: &Path,
    op: &Operation,
) -> Result<(Vec<Entry>, u64)> {
    op.check()?;
    let file_len = file.seek(SeekFrom::End(0))?;
    if file_len < 32768 + SECTOR_SIZE {
        return Err(ArcError::new(
            "CORRUPT",
            "File is too small to be a valid ISO image",
        ));
    }

    let mut pvd_root: Option<RootRecord> = None;
    let mut joliet_root: Option<RootRecord> = None;

    let mut sector_buf = [0u8; SECTOR_SIZE as usize];
    for sector in 16..64 {
        op.check()?;
        let pos = (sector as u64) * SECTOR_SIZE;
        if pos + SECTOR_SIZE > file_len {
            break;
        }
        file.seek(SeekFrom::Start(pos))?;
        if file.read_exact(&mut sector_buf).is_err() {
            break;
        }

        let vd_type = sector_buf[0];
        let standard_id = &sector_buf[1..6];
        if standard_id != b"CD001" {
            continue;
        }

        if vd_type == 1 && pvd_root.is_none() {
            pvd_root = Some(parse_root_record(&sector_buf[156..156 + 34])?);
        } else if vd_type == 2 && joliet_root.is_none() {
            let escape = &sector_buf[88..91];
            if escape == b"%/@"
                || escape == b"%/C"
                || escape == b"%/E"
                || &sector_buf[88..90] == b"%/"
            {
                joliet_root = Some(parse_root_record(&sector_buf[156..156 + 34])?);
            }
        } else if vd_type == 255 {
            break;
        }
    }

    let (use_joliet, root) = match (joliet_root, pvd_root) {
        (Some(svd), _) => (true, svd),
        (None, Some(pvd)) => (false, pvd),
        (None, None) => {
            return Err(ArcError::new(
                "CORRUPT",
                "No valid ISO 9660 Primary or Joliet Volume Descriptor found",
            ));
        }
    };

    let policy = Policy::default();
    let mut entries = Vec::new();
    let mut total_size: u64 = 0;
    let mut entry_index: u32 = 0;

    struct DirQueueItem {
        lba: u32,
        len: u32,
        parent_path: String,
        depth: usize,
    }

    let mut queue = VecDeque::new();
    let mut visited_dirs = HashSet::new();

    visited_dirs.insert(root.lba);
    queue.push_back(DirQueueItem {
        lba: root.lba,
        len: root.len,
        parent_path: String::new(),
        depth: 0,
    });

    while let Some(dir_item) = queue.pop_front() {
        op.check()?;
        let num_sectors = (dir_item.len as u64).div_ceil(SECTOR_SIZE);
        if num_sectors == 0 {
            continue;
        }
        if num_sectors > 16384 {
            return Err(ArcError::new(
                "LIMIT",
                "Directory extent exceeds maximum size",
            ));
        }

        for sector_idx in 0..num_sectors {
            op.check()?;
            let sec_lba = dir_item.lba as u64 + sector_idx;
            let sec_pos = sec_lba * SECTOR_SIZE;
            if sec_pos + SECTOR_SIZE > file_len {
                return Err(ArcError::new(
                    "CORRUPT",
                    "Directory extent extends beyond end of file",
                ));
            }
            file.seek(SeekFrom::Start(sec_pos))?;
            file.read_exact(&mut sector_buf)?;

            let mut offset = 0;
            while offset < sector_buf.len() {
                let rec_len = sector_buf[offset] as usize;
                if rec_len == 0 {
                    // Padding bytes to sector boundary
                    break;
                }
                if offset + rec_len > sector_buf.len() || rec_len < 33 {
                    return Err(ArcError::new("CORRUPT", "Malformed directory record"));
                }
                let rec = &sector_buf[offset..offset + rec_len];
                let extent_lba = u32::from_le_bytes(rec[2..6].try_into().unwrap());
                let data_len = u32::from_le_bytes(rec[10..14].try_into().unwrap());
                let file_flags = rec[25];
                let is_dir = (file_flags & 0x02) != 0;
                let len_fi = rec[32] as usize;
                if 33 + len_fi > rec_len {
                    return Err(ArcError::new(
                        "CORRUPT",
                        "File identifier extends past record",
                    ));
                }
                let fi = &rec[33..33 + len_fi];
                offset += rec_len;

                // Skip current (.) and parent (..) directory records
                let is_current_or_parent = (len_fi == 1 && (fi[0] == 0 || fi[0] == 1))
                    || (len_fi == 2 && fi[0] == 0 && (fi[1] == 0 || fi[1] == 1));
                if is_current_or_parent {
                    continue;
                }

                let mut name = if use_joliet {
                    let u16_chars: Vec<u16> = fi
                        .as_chunks::<2>()
                        .0
                        .iter()
                        .map(|pair| u16::from_be_bytes(*pair))
                        .collect();
                    String::from_utf16_lossy(&u16_chars)
                } else {
                    String::from_utf8_lossy(fi).into_owned()
                };
                name = name.trim_end_matches('\0').to_string();

                // Strip ISO 9660 / Joliet version suffix (;1 or ;<digits>)
                if let Some(pos) = name.rfind(';')
                    && name[pos + 1..].chars().all(|c| c.is_ascii_digit())
                {
                    name.truncate(pos);
                }
                if name.ends_with('.') {
                    name.pop();
                }
                if name.is_empty() {
                    continue;
                }

                let full_path = if dir_item.parent_path.is_empty() {
                    name
                } else {
                    format!("{}/{}", dir_item.parent_path, name)
                };

                let safe_name = validate_name(&full_path)?;
                let (date_str, date_unix) = parse_iso_datetime(&rec[18..25]);

                if is_dir {
                    if dir_item.depth + 1 > 64 {
                        return Err(ArcError::new(
                            "LIMIT",
                            "Directory hierarchy exceeds maximum depth",
                        ));
                    }
                    if visited_dirs.contains(&extent_lba) {
                        return Err(ArcError::new(
                            "LIMIT",
                            "Directory hierarchy exceeds maximum depth",
                        ));
                    }
                    visited_dirs.insert(extent_lba);

                    let id = ((extent_lba as u64) << 32) | (entry_index as u64);
                    entry_index += 1;

                    entries.push(Entry {
                        id,
                        name: safe_name.clone(),
                        size: 0,
                        packed: 0,
                        directory: true,
                        link: false,
                        encrypted: false,
                        crc: None,
                        modified: date_str,
                        modified_unix: date_unix,
                    });

                    queue.push_back(DirQueueItem {
                        lba: extent_lba,
                        len: data_len,
                        parent_path: safe_name,
                        depth: dir_item.depth + 1,
                    });
                } else {
                    let file_size = data_len as u64;
                    if file_size > policy.max_file_bytes {
                        return Err(ArcError::new("LIMIT", "File exceeds maximum size"));
                    }
                    total_size = total_size
                        .checked_add(file_size)
                        .ok_or_else(|| ArcError::new("LIMIT", "Archive size overflow"))?;
                    if total_size > policy.max_total_bytes {
                        return Err(ArcError::new(
                            "LIMIT",
                            "Total unpacked size exceeds configured limit",
                        ));
                    }

                    let extent_end = (extent_lba as u64)
                        .checked_mul(SECTOR_SIZE)
                        .and_then(|s| s.checked_add(file_size));
                    if let Some(end) = extent_end {
                        if end > file_len {
                            return Err(ArcError::new(
                                "CORRUPT",
                                "File extent extends beyond end of file",
                            ));
                        }
                    } else {
                        return Err(ArcError::new("CORRUPT", "File extent offset overflow"));
                    }

                    let id = ((extent_lba as u64) << 32) | (entry_index as u64);
                    entry_index += 1;

                    entries.push(Entry {
                        id,
                        name: safe_name,
                        size: file_size,
                        packed: file_size,
                        directory: false,
                        link: false,
                        encrypted: false,
                        crc: None,
                        modified: date_str,
                        modified_unix: date_unix,
                    });
                }

                if entries.len() > policy.max_entries {
                    return Err(ArcError::new("LIMIT", "Too many entries"));
                }
            }
        }
    }

    Ok((entries, total_size))
}

/// Read a single file entry from the ISO image, seeking directly to its extent and streaming payload.
pub fn read_entry(
    file: &mut (impl Read + Seek),
    entry: &Entry,
    op: &Operation,
    consume: impl FnOnce(&Entry, &mut dyn Read) -> Result<()>,
) -> Result<()> {
    op.check()?;
    if entry.directory {
        return Err(ArcError::new(
            "SELECTION",
            "Cannot read directory entry as stream",
        ));
    }

    let extent_lba = if (entry.id >> 32) != 0 {
        (entry.id >> 32) as u32
    } else {
        let (entries, _) = list(file, Path::new(""), op)?;
        let found = entries
            .iter()
            .find(|e| e.name == entry.name && !e.directory)
            .ok_or_else(|| ArcError::new("SELECTION", "Entry not found in ISO image"))?;
        (found.id >> 32) as u32
    };

    let byte_offset = (extent_lba as u64) * SECTOR_SIZE;
    let file_len = file.seek(SeekFrom::End(0))?;
    if byte_offset
        .checked_add(entry.size)
        .is_none_or(|end| end > file_len)
    {
        return Err(ArcError::new(
            "CORRUPT",
            "File extent extends beyond end of file",
        ));
    }

    file.seek(SeekFrom::Start(byte_offset))?;
    let mut reader = file.by_ref().take(entry.size);
    consume(entry, &mut reader)?;
    op.check()
}

/// Stream all non-directory entries sequentially from the ISO image.
pub fn stream(
    file: &mut (impl Read + Seek),
    entries: &[Entry],
    op: &Operation,
    mut consume: impl FnMut(&Entry, &mut dyn Read) -> Result<()>,
) -> Result<()> {
    for entry in entries {
        op.check()?;
        if entry.directory {
            continue;
        }
        read_entry(file, entry, op, &mut consume)?;
    }
    Ok(())
}
