use std::io::Cursor;
use vynx_arc_core::{Operation, wim};

fn make_synthetic_wim() -> Vec<u8> {
    let mut data = vec![0u8; 1024];
    // Magic: MSWIM\0\0\0
    data[0..8].copy_from_slice(b"MSWIM\0\0\0");
    // Header size: 208 (0xD0)
    data[8..12].copy_from_slice(&208u32.to_le_bytes());
    // Version: 0x00010d00
    data[12..16].copy_from_slice(&0x00010d00u32.to_le_bytes());
    // Flags: 0x00000002 (Compressed)
    data[16..20].copy_from_slice(&2u32.to_le_bytes());
    // Part number: 1, Total parts: 1
    data[40..42].copy_from_slice(&1u16.to_le_bytes());
    data[42..44].copy_from_slice(&1u16.to_le_bytes());
    // Image count: 1
    data[44..48].copy_from_slice(&1u32.to_le_bytes());

    // XML data at offset 512, size 256
    let xml = "<WIM><IMAGE INDEX=\"1\"><NAME>Test Image</NAME><TOTALBYTES>100</TOTALBYTES><FILECOUNT>1</FILECOUNT></IMAGE></WIM>";
    let xml_u16: Vec<u8> = xml.encode_utf16().flat_map(|u| u.to_le_bytes()).collect();
    let xml_offset = 512u64;
    let xml_len = xml_u16.len() as u64;

    // XML resource descriptor in header (offset 72..96)
    // 8 bytes original size, 8 bytes flags (0x02 uncompressed in resource), 8 bytes offset
    data[72..80].copy_from_slice(&xml_len.to_le_bytes());
    data[80..88].copy_from_slice(&0x02u64.to_le_bytes());
    data[88..96].copy_from_slice(&xml_offset.to_le_bytes());

    data.resize(512 + xml_u16.len(), 0);
    data[512..512 + xml_u16.len()].copy_from_slice(&xml_u16);
    data
}

fn make_multi_image_wim() -> Vec<u8> {
    let mut data = vec![0u8; 1024];
    data[0..8].copy_from_slice(b"MSWIM\0\0\0");
    data[8..12].copy_from_slice(&208u32.to_le_bytes());
    data[12..16].copy_from_slice(&0x00010d00u32.to_le_bytes());
    // Flags: LZX compressed (0x00020002)
    data[16..20].copy_from_slice(&0x00020002u32.to_le_bytes());
    data[40..42].copy_from_slice(&1u16.to_le_bytes());
    data[42..44].copy_from_slice(&1u16.to_le_bytes());
    data[44..48].copy_from_slice(&2u32.to_le_bytes()); // 2 images

    let xml = r#"<WIM>
        <IMAGE INDEX="1">
            <NAME>Windows 11 Home</NAME>
            <DESCRIPTION>Home Edition</DESCRIPTION>
            <TOTALBYTES>4000</TOTALBYTES>
            <FILECOUNT>20</FILECOUNT>
            <DIRCOUNT>5</DIRCOUNT>
        </IMAGE>
        <IMAGE INDEX="2">
            <NAME>Windows 11 Pro</NAME>
            <DESCRIPTION>Pro Edition</DESCRIPTION>
            <TOTALBYTES>6000</TOTALBYTES>
            <FILECOUNT>30</FILECOUNT>
            <DIRCOUNT>8</DIRCOUNT>
        </IMAGE>
    </WIM>"#;
    let xml_u16: Vec<u8> = xml.encode_utf16().flat_map(|u| u.to_le_bytes()).collect();
    let xml_offset = 512u64;
    let xml_len = xml_u16.len() as u64;

    data[72..80].copy_from_slice(&xml_len.to_le_bytes());
    data[80..88].copy_from_slice(&0x02u64.to_le_bytes());
    data[88..96].copy_from_slice(&xml_offset.to_le_bytes());

    data.resize(512 + xml_u16.len(), 0);
    data[512..512 + xml_u16.len()].copy_from_slice(&xml_u16);
    data
}

#[test]
fn test_wim_detect_and_header() {
    let raw = make_synthetic_wim();
    let mut cursor = Cursor::new(raw);
    assert!(wim::detect(&mut cursor).expect("detect"));
    let op = Operation::default();
    let (entries, total_size) =
        wim::list(&mut cursor, std::path::Path::new("test.wim"), &op).expect("list");
    assert!(!entries.is_empty());
    assert_eq!(entries[0].name, "[1] Test Image");
    assert_eq!(entries[0].size, 100);
    assert_eq!(total_size, 100);
}

#[test]
fn test_wim_multi_image_list() {
    let raw = make_multi_image_wim();
    let mut cursor = Cursor::new(raw);
    assert!(wim::detect(&mut cursor).expect("detect"));
    let header = wim::parse_header(&mut cursor).expect("parse_header");
    assert_eq!(header.image_count, 2);
    assert_eq!(header.compression, wim::WimCompression::Lzx);

    let op = Operation::default();
    let (entries, total_size) =
        wim::list(&mut cursor, std::path::Path::new("install.wim"), &op).expect("list");
    assert_eq!(entries.len(), 2);
    assert_eq!(entries[0].name, "[1] Windows 11 Home");
    assert_eq!(entries[0].size, 4000);
    assert_eq!(entries[1].name, "[2] Windows 11 Pro");
    assert_eq!(entries[1].size, 6000);
    assert_eq!(total_size, 10000);
}

#[test]
fn test_wim_corrupt_magic() {
    let mut raw = make_synthetic_wim();
    raw[0..4].copy_from_slice(b"BAD!");
    let mut cursor = Cursor::new(raw);
    assert!(!wim::detect(&mut cursor).expect("detect"));
    assert!(wim::parse_header(&mut cursor).is_err());
}

#[test]
fn test_wim_truncated_header() {
    let raw = vec![0u8; 100]; // less than 208 bytes
    let mut cursor = Cursor::new(raw);
    assert!(!wim::detect(&mut cursor).expect("detect"));
    assert!(wim::parse_header(&mut cursor).is_err());
}

#[test]
fn test_wim_xml_out_of_bounds() {
    let mut raw = make_synthetic_wim();
    // Point XML offset past end of file
    raw[88..96].copy_from_slice(&99999u64.to_le_bytes());
    let mut cursor = Cursor::new(raw);
    let op = Operation::default();
    let err = wim::list(&mut cursor, std::path::Path::new("test.wim"), &op);
    assert!(err.is_err());
}

#[test]
fn test_wim_unsafe_image_name_rejected() {
    let mut data = vec![0u8; 1024];
    data[0..8].copy_from_slice(b"MSWIM\0\0\0");
    data[8..12].copy_from_slice(&208u32.to_le_bytes());
    data[12..16].copy_from_slice(&0x00010d00u32.to_le_bytes());
    data[16..20].copy_from_slice(&2u32.to_le_bytes());
    data[40..42].copy_from_slice(&1u16.to_le_bytes());
    data[42..44].copy_from_slice(&1u16.to_le_bytes());
    data[44..48].copy_from_slice(&1u32.to_le_bytes());

    let xml = "<WIM><IMAGE INDEX=\"1\"><NAME>../evil/escape</NAME><TOTALBYTES>100</TOTALBYTES></IMAGE></WIM>";
    let xml_u16: Vec<u8> = xml.encode_utf16().flat_map(|u| u.to_le_bytes()).collect();
    let xml_offset = 512u64;
    let xml_len = xml_u16.len() as u64;

    data[72..80].copy_from_slice(&xml_len.to_le_bytes());
    data[80..88].copy_from_slice(&0x02u64.to_le_bytes());
    data[88..96].copy_from_slice(&xml_offset.to_le_bytes());

    data.resize(512 + xml_u16.len(), 0);
    data[512..512 + xml_u16.len()].copy_from_slice(&xml_u16);

    let mut cursor = Cursor::new(data);
    let op = Operation::default();
    let res = wim::list(&mut cursor, std::path::Path::new("evil.wim"), &op);
    assert!(res.is_err(), "Path traversal in image name must be rejected");
}

#[test]
fn test_wim_standard_disk_reshdr() {
    let mut data = vec![0u8; 1024];
    data[0..8].copy_from_slice(b"MSWIM\0\0\0");
    data[8..12].copy_from_slice(&208u32.to_le_bytes());
    data[12..16].copy_from_slice(&0x00010d00u32.to_le_bytes());
    data[16..20].copy_from_slice(&0x00040002u32.to_le_bytes()); // XPRESS
    data[40..42].copy_from_slice(&1u16.to_le_bytes());
    data[42..44].copy_from_slice(&1u16.to_le_bytes());
    data[44..48].copy_from_slice(&1u32.to_le_bytes());

    let xml = "<WIM><IMAGE INDEX=\"1\"><NAME>Standard WIM</NAME><TOTALBYTES>500</TOTALBYTES></IMAGE></WIM>";
    let xml_u16: Vec<u8> = xml.encode_utf16().flat_map(|u| u.to_le_bytes()).collect();
    let xml_offset = 512u64;
    let xml_len = xml_u16.len() as u64;

    // Standard on-disk RESHDR layout:
    // 0..7: size_in_wim (7 bytes)
    // 7: flags (0x02)
    // 8..16: offset_in_wim (8 bytes)
    // 16..24: uncompressed_size (8 bytes)
    let size_bytes = xml_len.to_le_bytes();
    data[72..79].copy_from_slice(&size_bytes[0..7]);
    data[79] = 0x02;
    data[80..88].copy_from_slice(&xml_offset.to_le_bytes());
    data[88..96].copy_from_slice(&xml_len.to_le_bytes());

    data.resize(512 + xml_u16.len(), 0);
    data[512..512 + xml_u16.len()].copy_from_slice(&xml_u16);

    let mut cursor = Cursor::new(data);
    let header = wim::parse_header(&mut cursor).expect("parse_header");
    assert_eq!(header.compression, wim::WimCompression::Xpress);
    assert_eq!(header.xml_data.offset, 512);

    let op = Operation::default();
    let (entries, total_size) =
        wim::list(&mut cursor, std::path::Path::new("std.wim"), &op).expect("list");
    assert_eq!(entries.len(), 1);
    assert_eq!(entries[0].name, "[1] Standard WIM");
    assert_eq!(total_size, 500);
}
