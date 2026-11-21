//! Pure safe Rust parser for Windows Imaging Format (WIM / SWM / ESD) headers and XML metadata.

use crate::{ArcError, Entry, Result, operations::Operation, security::validate_name};
use std::{
    io::{Read, Seek, SeekFrom},
    path::Path,
};

pub use crate::wim_native::extract_image;

/// Header size in bytes of a standard WIM v1 file.
pub const WIM_HEADER_SIZE: usize = 208;

/// WIM magic bytes at offset 0.
pub const WIM_MAGIC: &[u8; 8] = b"MSWIM\0\0\0";

/// WIM header flag bits.
pub const WIM_FLAG_RESERVED: u32 = 0x0000_0001;
pub const WIM_FLAG_COMPRESSION: u32 = 0x0000_0002;
pub const WIM_FLAG_READONLY: u32 = 0x0000_0004;
pub const WIM_FLAG_SPANNED: u32 = 0x0000_0008;
pub const WIM_FLAG_RESOURCE_ONLY: u32 = 0x0000_0010;
pub const WIM_FLAG_METADATA_ONLY: u32 = 0x0000_0020;
pub const WIM_FLAG_WRITE_IN_PROGRESS: u32 = 0x0000_0040;
pub const WIM_FLAG_RP_FIX: u32 = 0x0000_0080;
pub const WIM_FLAG_COMPRESS_RESERVED: u32 = 0x0001_0000;
pub const WIM_FLAG_COMPRESS_LZX: u32 = 0x0002_0000;
pub const WIM_FLAG_COMPRESS_XPRESS: u32 = 0x0004_0000;
pub const WIM_FLAG_COMPRESS_LZMS: u32 = 0x0008_0000;

/// Maximum allowable size for XML metadata stream (64 MB) to prevent denial of service.
pub const MAX_XML_SIZE: u64 = 64 * 1024 * 1024;

/// Compression method employed by the WIM archive.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WimCompression {
    None,
    Xpress,
    Lzx,
    Lzms,
}

/// A 24-byte WIM resource descriptor (header/lookup entry).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub struct WimResHdr {
    /// Compressed/stored size of the resource within the WIM file.
    pub size_in_wim: u64,
    /// Resource flags (e.g. metadata = 0x02, compressed = 0x04).
    pub flags: u64,
    /// Absolute byte offset from the start of the WIM file.
    pub offset: u64,
    /// Uncompressed size of the resource.
    pub uncompressed_size: u64,
}

/// Parsed 208-byte WIM file header.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct WimHeader {
    pub header_size: u32,
    pub version: u32,
    pub flags: u32,
    pub compression: WimCompression,
    pub chunk_size: u32,
    pub guid: [u8; 16],
    pub part_number: u16,
    pub total_parts: u16,
    pub image_count: u32,
    pub offset_table: WimResHdr,
    pub xml_data: WimResHdr,
    pub boot_metadata: WimResHdr,
    pub boot_index: u32,
    pub integrity: WimResHdr,
}

/// Image metadata extracted from the WIM UTF-16LE XML manifest.
#[derive(Clone, Debug, PartialEq, Eq, Default)]
pub struct WimImageInfo {
    pub index: u32,
    pub name: String,
    pub description: String,
    pub total_bytes: u64,
    pub file_count: u64,
    pub dir_count: u64,
}

fn parse_resource_header(b: &[u8; 24], file_len: u64) -> WimResHdr {
    if b.iter().all(|&x| x == 0) {
        return WimResHdr::default();
    }

    // Layout A (Brief synthetic format):
    // 0..8: size
    // 8..16: flags
    // 16..24: offset
    let size_a = u64::from_le_bytes(b[0..8].try_into().unwrap());
    let flags_a = u64::from_le_bytes(b[8..16].try_into().unwrap());
    let offset_a = u64::from_le_bytes(b[16..24].try_into().unwrap());

    // Layout B (Standard Microsoft WIM on-disk format):
    // 0..7: size_in_wim (56 bits)
    // 7: flags (8 bits)
    // 8..16: offset_in_wim (64 bits)
    // 16..24: uncompressed_size (64 bits)
    let mut size_b_bytes = [0u8; 8];
    size_b_bytes[0..7].copy_from_slice(&b[0..7]);
    let size_b = u64::from_le_bytes(size_b_bytes);
    let flags_b = b[7] as u64;
    let offset_b = u64::from_le_bytes(b[8..16].try_into().unwrap());
    let uncompressed_size_b = u64::from_le_bytes(b[16..24].try_into().unwrap());

    if offset_b >= 208 && offset_b <= file_len {
        WimResHdr {
            size_in_wim: size_b,
            flags: flags_b,
            offset: offset_b,
            uncompressed_size: uncompressed_size_b,
        }
    } else if offset_a >= 208 && offset_a <= file_len {
        WimResHdr {
            size_in_wim: size_a,
            flags: flags_a,
            offset: offset_a,
            uncompressed_size: size_a,
        }
    } else if offset_b <= file_len {
        WimResHdr {
            size_in_wim: size_b,
            flags: flags_b,
            offset: offset_b,
            uncompressed_size: uncompressed_size_b,
        }
    } else {
        WimResHdr {
            size_in_wim: size_a,
            flags: flags_a,
            offset: offset_a,
            uncompressed_size: size_a,
        }
    }
}

/// Detect if the stream starts with a valid WIM header.
pub fn detect(file: &mut (impl Read + Seek)) -> Result<bool> {
    let initial_pos = file.stream_position()?;
    let length = file.seek(SeekFrom::End(0))?;
    if length < 208 {
        file.seek(SeekFrom::Start(initial_pos))?;
        return Ok(false);
    }
    file.seek(SeekFrom::Start(0))?;
    let mut header = [0u8; 12];
    if file.read_exact(&mut header).is_err() {
        file.seek(SeekFrom::Start(initial_pos))?;
        return Ok(false);
    }
    file.seek(SeekFrom::Start(initial_pos))?;
    let magic_matches = &header[0..8] == WIM_MAGIC;
    let size = u32::from_le_bytes(header[8..12].try_into().unwrap());
    Ok(magic_matches && size >= 208)
}

/// Parse the 208-byte WIM header and its resource descriptors.
pub fn parse_header(file: &mut (impl Read + Seek)) -> Result<WimHeader> {
    let file_len = file.seek(SeekFrom::End(0))?;
    if file_len < 208 {
        return Err(ArcError::new(
            "CORRUPT",
            "File is smaller than WIM header size",
        ));
    }
    file.seek(SeekFrom::Start(0))?;
    let mut buf = [0u8; 208];
    file.read_exact(&mut buf)?;

    if &buf[0..8] != WIM_MAGIC {
        return Err(ArcError::new("CORRUPT", "Invalid WIM header magic"));
    }

    let header_size = u32::from_le_bytes(buf[8..12].try_into().unwrap());
    if header_size < 208 {
        return Err(ArcError::new(
            "CORRUPT",
            format!("Invalid WIM header size: {header_size} < 208"),
        ));
    }

    let version = u32::from_le_bytes(buf[12..16].try_into().unwrap());
    if (version >> 16) == 0 {
        return Err(ArcError::new(
            "CORRUPT",
            format!("Invalid WIM version: {version:#x}"),
        ));
    }

    let flags = u32::from_le_bytes(buf[16..20].try_into().unwrap());
    let compression = if flags & WIM_FLAG_COMPRESS_LZMS != 0 {
        WimCompression::Lzms
    } else if flags & WIM_FLAG_COMPRESS_LZX != 0 {
        WimCompression::Lzx
    } else if flags & (WIM_FLAG_COMPRESS_XPRESS | WIM_FLAG_COMPRESSION) != 0 {
        WimCompression::Xpress
    } else {
        WimCompression::None
    };

    let chunk_size = u32::from_le_bytes(buf[20..24].try_into().unwrap());
    let mut guid = [0u8; 16];
    guid.copy_from_slice(&buf[24..40]);
    let part_number = u16::from_le_bytes(buf[40..42].try_into().unwrap());
    let total_parts = u16::from_le_bytes(buf[42..44].try_into().unwrap());
    let image_count = u32::from_le_bytes(buf[44..48].try_into().unwrap());

    let offset_table = parse_resource_header(buf[48..72].try_into().unwrap(), file_len);
    let xml_data = parse_resource_header(buf[72..96].try_into().unwrap(), file_len);
    let boot_metadata = parse_resource_header(buf[96..120].try_into().unwrap(), file_len);
    let boot_index = u32::from_le_bytes(buf[120..124].try_into().unwrap());
    let integrity = parse_resource_header(buf[124..148].try_into().unwrap(), file_len);

    Ok(WimHeader {
        header_size,
        version,
        flags,
        compression,
        chunk_size,
        guid,
        part_number,
        total_parts,
        image_count,
        offset_table,
        xml_data,
        boot_metadata,
        boot_index,
        integrity,
    })
}

fn decode_xml_bytes(bytes: &[u8]) -> String {
    if bytes.starts_with(&[0xff, 0xfe]) {
        let u16s: Vec<u16> = bytes[2..]
            .as_chunks::<2>()
            .0
            .iter()
            .map(|&[b0, b1]| u16::from_le_bytes([b0, b1]))
            .collect();
        String::from_utf16_lossy(&u16s)
    } else if bytes.starts_with(&[0xfe, 0xff]) {
        let u16s: Vec<u16> = bytes[2..]
            .as_chunks::<2>()
            .0
            .iter()
            .map(|&[b0, b1]| u16::from_be_bytes([b0, b1]))
            .collect();
        String::from_utf16_lossy(&u16s)
    } else if bytes.len() >= 2 && bytes[1] == 0 {
        let u16s: Vec<u16> = bytes
            .as_chunks::<2>()
            .0
            .iter()
            .map(|&[b0, b1]| u16::from_le_bytes([b0, b1]))
            .collect();
        String::from_utf16_lossy(&u16s)
    } else if bytes.starts_with(&[0xef, 0xbb, 0xbf]) {
        String::from_utf8_lossy(&bytes[3..]).to_string()
    } else {
        String::from_utf8_lossy(bytes).to_string()
    }
}

fn clean_xml_text(raw: &str) -> String {
    let mut s = raw.trim();
    if s.starts_with("<![CDATA[") && s.ends_with("]]>") && s.len() >= 12 {
        s = &s[9..s.len() - 3];
        s = s.trim();
    }
    s.replace("&amp;", "&")
        .replace("&lt;", "<")
        .replace("&gt;", ">")
        .replace("&quot;", "\"")
        .replace("&apos;", "'")
}

fn parse_image_index(tag_header: &str, fallback_index: u32) -> u32 {
    let lower = tag_header.to_ascii_lowercase();
    if let Some(pos) = lower.find("index") {
        let after = &tag_header[pos + 5..];
        let trimmed = after.trim_start();
        if let Some(stripped) = trimmed.strip_prefix('=') {
            let val_part = stripped.trim_start();
            let quote = val_part.chars().next();
            if quote == Some('"') || quote == Some('\'') {
                let quote_char = quote.unwrap();
                let inner = &val_part[1..];
                if let Some(end_quote) = inner.find(quote_char)
                    && let Ok(idx) = inner[..end_quote].trim().parse::<u32>()
                {
                    return idx;
                }
            } else {
                let digits: String = val_part
                    .chars()
                    .take_while(|c| c.is_ascii_digit())
                    .collect();
                if let Ok(idx) = digits.parse::<u32>() {
                    return idx;
                }
            }
        }
    }
    fallback_index
}

fn extract_tag_value(block: &str, tag_name: &str) -> Option<String> {
    let tag_open = format!("<{}", tag_name);
    let tag_close = format!("</{}>", tag_name);
    let block_lower = block.to_ascii_lowercase();
    let tag_open_lower = tag_open.to_ascii_lowercase();
    let tag_close_lower = tag_close.to_ascii_lowercase();

    let mut search_idx = 0;
    while let Some(start_pos) = block_lower[search_idx..].find(&tag_open_lower) {
        let abs_start = search_idx + start_pos;
        let after_tag_idx = abs_start + tag_open_lower.len();
        if after_tag_idx < block.len() {
            let next_ch = block.as_bytes()[after_tag_idx];
            if matches!(next_ch, b'>' | b' ' | b'\t' | b'\r' | b'\n' | b'/')
                && let Some(open_tag_end) = block[after_tag_idx..].find('>')
            {
                let content_start = after_tag_idx + open_tag_end + 1;
                if let Some(close_pos) = block_lower[content_start..].find(&tag_close_lower) {
                    let content_end = content_start + close_pos;
                    let content = &block[content_start..content_end];
                    return Some(clean_xml_text(content));
                }
            }
        }
        search_idx = abs_start + 1;
    }
    None
}

fn extract_number(block: &str, tag_name: &str) -> u64 {
    if let Some(val) = extract_tag_value(block, tag_name) {
        let trimmed = val.trim();
        if let Some(hex) = trimmed
            .strip_prefix("0x")
            .or_else(|| trimmed.strip_prefix("0X"))
        {
            u64::from_str_radix(hex, 16).unwrap_or(0)
        } else {
            trimmed.parse::<u64>().unwrap_or(0)
        }
    } else {
        0
    }
}

fn parse_images_xml(xml: &str) -> Vec<WimImageInfo> {
    let mut images = Vec::new();
    let xml_lower = xml.to_ascii_lowercase();
    let mut search_idx = 0;
    let mut fallback_index = 1;

    while let Some(start_pos) = xml_lower[search_idx..].find("<image") {
        let abs_start = search_idx + start_pos;
        let after_tag_idx = abs_start + 6;
        if after_tag_idx < xml.len() {
            let next_ch = xml.as_bytes()[after_tag_idx];
            if matches!(next_ch, b'>' | b' ' | b'\t' | b'\r' | b'\n')
                && let Some(open_end) = xml[after_tag_idx..].find('>')
            {
                let tag_header = &xml[abs_start..after_tag_idx + open_end + 1];
                let content_start = after_tag_idx + open_end + 1;
                if let Some(close_pos) = xml_lower[content_start..].find("</image>") {
                    let content_end = content_start + close_pos;
                    let block = &xml[content_start..content_end];
                    let index = parse_image_index(tag_header, fallback_index);
                    fallback_index = index.saturating_add(1);

                    let name = extract_tag_value(block, "name").unwrap_or_default();
                    let description = extract_tag_value(block, "description").unwrap_or_default();
                    let total_bytes = extract_number(block, "totalbytes");
                    let file_count = extract_number(block, "filecount");
                    let dir_count = extract_number(block, "dircount");

                    images.push(WimImageInfo {
                        index,
                        name,
                        description,
                        total_bytes,
                        file_count,
                        dir_count,
                    });

                    search_idx = content_end + 8;
                    continue;
                }
            }
        }
        search_idx = abs_start + 1;
    }

    images
}

/// Parse UTF-16LE XML metadata from the resource indicated by the header.
pub fn parse_xml_metadata(
    file: &mut (impl Read + Seek),
    header: &WimHeader,
) -> Result<Vec<WimImageInfo>> {
    let file_len = file.seek(SeekFrom::End(0))?;
    let xml_size = if header.xml_data.size_in_wim > 0 {
        header.xml_data.size_in_wim
    } else {
        header.xml_data.uncompressed_size
    };

    if xml_size == 0 || header.xml_data.offset == 0 {
        if header.image_count == 0 {
            return Ok(Vec::new());
        }
        return Err(ArcError::new(
            "CORRUPT",
            "WIM XML metadata resource is empty",
        ));
    }

    if xml_size > MAX_XML_SIZE {
        return Err(ArcError::new(
            "LIMIT",
            format!("XML metadata resource size ({xml_size}) exceeds 64MB limit"),
        ));
    }

    let xml_end = header
        .xml_data
        .offset
        .checked_add(xml_size)
        .ok_or_else(|| ArcError::new("CORRUPT", "XML resource offset overflow"))?;

    if xml_end > file_len {
        return Err(ArcError::new(
            "CORRUPT",
            "XML resource extends past end of file",
        ));
    }

    file.seek(SeekFrom::Start(header.xml_data.offset))?;
    let mut raw_xml = vec![0u8; xml_size as usize];
    file.read_exact(&mut raw_xml)?;

    let xml_str = decode_xml_bytes(&raw_xml);
    let images = parse_images_xml(&xml_str);

    if images.is_empty() && header.image_count > 0 {
        return Err(ArcError::new(
            "CORRUPT",
            "No valid image metadata found in WIM XML",
        ));
    }

    Ok(images)
}

/// List all images in the WIM archive as directory entries, returning entries and total unpacked size.
pub fn list(
    file: &mut (impl Read + Seek),
    _path: &Path,
    op: &Operation,
) -> Result<(Vec<Entry>, u64)> {
    op.check()?;
    let header = parse_header(file)?;
    op.check()?;
    let images = parse_xml_metadata(file, &header)?;
    op.check()?;

    let mut entries = Vec::with_capacity(images.len());
    let mut total_size = 0u64;

    for img in images {
        op.check()?;
        let display_name = if img.name.trim().is_empty() {
            format!("Image {}", img.index)
        } else {
            img.name.trim().to_string()
        };

        let entry_name = format!("[{}] {}", img.index, display_name);
        let validated_name = validate_name(&entry_name)?;

        total_size = total_size
            .checked_add(img.total_bytes)
            .ok_or_else(|| ArcError::new("LIMIT", "WIM total size overflow"))?;

        entries.push(Entry {
            id: img.index as u64,
            name: validated_name,
            size: img.total_bytes,
            packed: 0,
            directory: true,
            link: false,
            encrypted: false,
            crc: None,
            modified: String::new(),
            modified_unix: None,
        });
    }

    Ok((entries, total_size))
}
