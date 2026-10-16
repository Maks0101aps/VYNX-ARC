//! Bounds metadata before handing untrusted ZIP declarations to the backend.
use crate::{ArcError, Result};
use std::io::{Read, Seek, SeekFrom};

pub const MAX_HEADER_BYTES: u64 = 32 << 20;
pub const MAX_METADATA_BYTES: u64 = 256 << 20;

fn limit(message: &str) -> ArcError {
    ArcError::new("LIMIT", message)
}
fn corrupt(message: &str) -> ArcError {
    ArcError::new("CORRUPT", message)
}
fn u16_at(bytes: &[u8], offset: usize) -> u64 {
    u16::from_le_bytes(bytes[offset..offset + 2].try_into().unwrap()) as u64
}
fn u32_at(bytes: &[u8], offset: usize) -> u64 {
    u32::from_le_bytes(bytes[offset..offset + 4].try_into().unwrap()) as u64
}
fn u64_at(bytes: &[u8], offset: usize) -> u64 {
    u64::from_le_bytes(bytes[offset..offset + 8].try_into().unwrap())
}

pub fn open_zip<R: Read + Seek>(mut reader: R) -> Result<zip::ZipArchive<R>> {
    preflight_zip(&mut reader)?;
    // Detection already requires a ZIP signature at byte zero. Disabling offset
    // guessing ensures the backend reads exactly the directory we bounded.
    let config = zip::read::Config {
        archive_offset: zip::read::ArchiveOffset::Known(0),
    };
    Ok(zip::ZipArchive::with_config(config, reader)?)
}

pub fn preflight_zip(reader: &mut (impl Read + Seek)) -> Result<()> {
    let length = reader.seek(SeekFrom::End(0))?;
    // Cover the backend's entire EOCD search window, including its extra byte.
    let tail_length = length.min(65536 + 22) as usize;
    reader.seek(SeekFrom::End(-(tail_length as i64)))?;
    let mut tail = vec![0; tail_length];
    reader.read_exact(&mut tail)?;
    let candidates = (0..tail.len().saturating_sub(21))
        .filter(|&i| tail[i..].starts_with(b"PK\x05\x06"))
        .count();
    // The backend may fall back to an earlier EOCD after a parse error, even
    // with Known(0). Reject ambiguity so it cannot select unbounded metadata
    // different from the single directory validated below.
    if candidates != 1 {
        return Err(corrupt("Missing or ambiguous ZIP end records"));
    }
    let end = (0..tail.len().saturating_sub(21))
        .rev()
        .find(|&i| {
            tail[i..].starts_with(b"PK\x05\x06")
                && i + 22 + u16_at(&tail, i + 20) as usize == tail.len()
        })
        .ok_or_else(|| corrupt("Missing ZIP end record or unsupported trailing data"))?;
    let end_offset = length - tail_length as u64 + end as u64;
    let end = &tail[end..];
    if u16_at(end, 4) != 0 || u16_at(end, 6) != 0 {
        return Err(ArcError::new("FORMAT", "Multi-disk ZIP is unsupported"));
    }
    let mut count = u16_at(end, 10);
    let mut size = u32_at(end, 12);
    let mut offset = u32_at(end, 16);
    let mut directory_end = end_offset;
    if count == 65535 || size == u32::MAX as u64 || offset == u32::MAX as u64 {
        let locator_offset = end_offset
            .checked_sub(20)
            .ok_or_else(|| corrupt("Missing ZIP64 locator"))?;
        reader.seek(SeekFrom::Start(locator_offset))?;
        let mut locator = [0; 20];
        reader.read_exact(&mut locator)?;
        if !locator.starts_with(b"PK\x06\x07")
            || u32_at(&locator, 4) != 0
            || u32_at(&locator, 16) != 1
        {
            return Err(corrupt("Invalid or multi-disk ZIP64 locator"));
        }
        directory_end = u64_at(&locator, 8);
        if directory_end
            .checked_add(56)
            .is_none_or(|n| n > locator_offset)
        {
            return Err(corrupt("ZIP64 end record is outside the archive"));
        }
        reader.seek(SeekFrom::Start(directory_end))?;
        let mut record = [0; 56];
        reader.read_exact(&mut record)?;
        let record_size = u64_at(&record, 4);
        if record_size > MAX_HEADER_BYTES {
            return Err(limit("ZIP64 extensible metadata exceeds the header budget"));
        }
        if !record.starts_with(b"PK\x06\x06")
            || record_size < 44
            || directory_end
                .checked_add(12)
                .and_then(|n| n.checked_add(record_size))
                != Some(locator_offset)
            || u32_at(&record, 16) != 0
            || u32_at(&record, 20) != 0
            || u64_at(&record, 24) != u64_at(&record, 32)
        {
            return Err(corrupt("Invalid ZIP64 end record"));
        }
        count = u64_at(&record, 32);
        size = u64_at(&record, 40);
        offset = u64_at(&record, 48);
    } else if u16_at(end, 8) != count {
        return Err(corrupt("ZIP entry counts disagree"));
    }
    // Conservative accounting for the backend's indexes, records, decoded names,
    // comments and extra fields. This is an input/allocation budget, not an RSS cap.
    let estimate = count
        .checked_mul(1024)
        .and_then(|n| size.checked_mul(4).and_then(|s| n.checked_add(s)));
    if size > MAX_HEADER_BYTES
        || count > crate::security::Policy::default().max_entries as u64
        || estimate.is_none_or(|n| n > MAX_METADATA_BYTES)
    {
        return Err(limit(
            "ZIP metadata exceeds the parsing budget (256 MiB accounting / 32 MiB directory)",
        ));
    }
    if offset.checked_add(size) != Some(directory_end) || count > size / 46 {
        return Err(corrupt("ZIP directory range or entry count is invalid"));
    }
    reader.seek(SeekFrom::Start(offset))?;
    let mut consumed = 0u64;
    for _ in 0..count {
        let mut header = [0; 46];
        reader.read_exact(&mut header)?;
        if !header.starts_with(b"PK\x01\x02") {
            return Err(corrupt("Invalid ZIP central directory entry"));
        }
        let variable = u16_at(&header, 28) + u16_at(&header, 30) + u16_at(&header, 32);
        consumed = consumed
            .checked_add(46 + variable)
            .filter(|n| *n <= size)
            .ok_or_else(|| corrupt("ZIP directory entry exceeds its declared range"))?;
        reader.seek(SeekFrom::Current(variable as i64))?;
    }
    if consumed != size {
        return Err(corrupt("ZIP directory size does not match its entries"));
    }
    reader.seek(SeekFrom::Start(0))?;
    Ok(())
}
