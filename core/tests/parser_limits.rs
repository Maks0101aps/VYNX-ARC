use std::{fs, io::Cursor};
use vynx_arc_core::{Archive, Operation, parser};

fn seven_header(header: &[u8], declared_size: u64) -> Vec<u8> {
    let mut start = Vec::new();
    start.extend(0u64.to_le_bytes());
    start.extend(declared_size.to_le_bytes());
    start.extend(crc32fast::hash(header).to_le_bytes());
    let mut bytes = b"7z\xbc\xaf\x27\x1c\x00\x04".to_vec();
    bytes.extend(crc32fast::hash(&start).to_le_bytes());
    bytes.extend(start);
    bytes.extend(header);
    bytes
}

#[test]
fn zip64_huge_count_is_rejected_before_backend_allocation() {
    let mut bytes = b"PK\x06\x06".to_vec();
    bytes.extend(44u64.to_le_bytes());
    bytes.extend([0; 12]);
    bytes.extend(u64::MAX.to_le_bytes());
    bytes.extend(u64::MAX.to_le_bytes());
    bytes.extend(0u64.to_le_bytes());
    bytes.extend(0u64.to_le_bytes());
    bytes.extend(b"PK\x06\x07");
    bytes.extend(0u32.to_le_bytes());
    bytes.extend(0u64.to_le_bytes());
    bytes.extend(1u32.to_le_bytes());
    bytes.extend(b"PK\x05\x06");
    bytes.extend([0; 4]);
    bytes.extend([255; 4]);
    bytes.extend([255; 8]);
    bytes.extend([0; 2]);
    let error = parser::preflight_zip(&mut Cursor::new(bytes)).unwrap_err();
    assert_eq!(error.code, "LIMIT");
}

#[test]
fn zip_directory_budget_rejects_sparse_large_directory() {
    let mut bytes = b"PK\x05\x06".to_vec();
    bytes.extend([0; 8]);
    bytes.extend(((parser::MAX_HEADER_BYTES + 1) as u32).to_le_bytes());
    bytes.extend([0; 6]);
    assert_eq!(
        parser::preflight_zip(&mut Cursor::new(bytes))
            .unwrap_err()
            .code,
        "LIMIT"
    );
}

#[test]
fn sevenz_rejects_declared_header_before_reading_or_allocating_it() {
    let t = tempfile::tempdir().unwrap();
    let path = t.path().join("oversized.7z");
    fs::write(&path, seven_header(&[], (16 << 20) + 1)).unwrap();
    let error = Archive::open(&path, "", &Operation::default())
        .err()
        .unwrap();
    assert!(error.message.contains("16 MiB"), "{error}");
}

#[test]
fn sevenz_metadata_budget_rejects_dense_file_table() {
    // 7z variable integer: prefix 0xf0 plus four little-endian bytes.
    let count = 1_000_000u32;
    let mut header = vec![1, 5, 0xf0]; // Header, FilesInfo, count
    header.extend(count.to_le_bytes());
    header.resize(count as usize + 16, 0);
    let t = tempfile::tempdir().unwrap();
    let path = t.path().join("dense.7z");
    fs::write(&path, seven_header(&header, header.len() as u64)).unwrap();
    let error = Archive::open(&path, "", &Operation::default())
        .err()
        .unwrap();
    assert!(error.message.contains("128 MiB"), "{error}");
}

#[test]
fn truncated_and_mutated_end_records_never_panic() {
    for length in 0..120 {
        let mut bytes = vec![0xff; length];
        if length >= 4 {
            bytes[..4].copy_from_slice(b"PK\x05\x06");
        }
        assert!(parser::preflight_zip(&mut Cursor::new(bytes)).is_err());
    }
}

#[test]
fn sevenz_encoded_header_and_dictionary_budgets_precede_decoding() {
    let prefix = [0x17, 6, 0, 1, 9, 1, 0, 7, 11, 1, 0];
    let mut dictionary = prefix.to_vec();
    dictionary.extend([1, 0x21, 0x21, 1, 40, 12, 1, 0, 0]);
    let mut decoded_size = prefix.to_vec();
    decoded_size.extend([1, 1, 0, 12, 0xff]); // one COPY coder; unpack size varint
    decoded_size.extend(((16u64 << 20) + 1).to_le_bytes());
    decoded_size.extend([0, 0]);
    let mut chain = prefix.to_vec();
    chain.extend([9; 20]); // rejected before allocating or parsing nine coders
    for (header, expected) in [
        (dictionary, "MaxMemLimited"),
        (decoded_size, "decoded header"),
        (chain, "coder chain"),
    ] {
        let t = tempfile::tempdir().unwrap();
        let path = t.path().join("encoded.7z");
        fs::write(&path, seven_header(&header, header.len() as u64)).unwrap();
        let error = Archive::open(&path, "", &Operation::default())
            .err()
            .unwrap();
        assert!(error.message.contains(expected), "{error}");
    }
}

#[test]
fn accepts_valid_zip64_and_archive_comment_without_offset_guessing() {
    use std::io::Write;
    let mut writer = zip::ZipWriter::new(Cursor::new(Vec::new()));
    writer
        .start_file("file.txt", zip::write::SimpleFileOptions::default())
        .unwrap();
    writer.write_all(b"data").unwrap();
    writer
        .set_comment("ZIP comment with PK\x05\x06 inside")
        .unwrap();
    let bytes = writer.finish().unwrap().into_inner();
    let mut normal = parser::open_zip(Cursor::new(bytes.clone())).unwrap();
    assert_eq!(normal.by_index(0).unwrap().name().unwrap(), "file.txt");
    let end = bytes.windows(4).position(|b| b == b"PK\x05\x06").unwrap();
    let directory_size = u32::from_le_bytes(bytes[end + 12..end + 16].try_into().unwrap());
    let directory_offset = u32::from_le_bytes(bytes[end + 16..end + 20].try_into().unwrap());
    let mut zip64 = bytes[..end].to_vec();
    zip64.extend(b"PK\x06\x06");
    zip64.extend(44u64.to_le_bytes());
    zip64.extend([45, 0, 45, 0]);
    zip64.extend([0; 8]);
    zip64.extend(1u64.to_le_bytes());
    zip64.extend(1u64.to_le_bytes());
    zip64.extend((directory_size as u64).to_le_bytes());
    zip64.extend((directory_offset as u64).to_le_bytes());
    zip64.extend(b"PK\x06\x07");
    zip64.extend(0u32.to_le_bytes());
    zip64.extend((end as u64).to_le_bytes());
    zip64.extend(1u32.to_le_bytes());
    let mut eocd = bytes[end..].to_vec();
    eocd[8..20].fill(255);
    zip64.extend(eocd);
    let mut parsed = parser::open_zip(Cursor::new(zip64)).unwrap();
    let mut payload = String::new();
    std::io::Read::read_to_string(&mut parsed.by_index(0).unwrap(), &mut payload).unwrap();
    assert_eq!(payload, "data");
}

#[test]
fn ambiguous_end_record_in_comment_cannot_trigger_backend_fallback() {
    let mut writer = zip::ZipWriter::new(Cursor::new(Vec::new()));
    let mut comment = b"PK\x05\x06".to_vec();
    comment.resize(22, 0);
    writer.set_raw_comment(comment.into()).unwrap();
    let bytes = writer.finish().unwrap().into_inner();
    let error = parser::preflight_zip(&mut Cursor::new(bytes)).unwrap_err();
    assert!(error.message.contains("ambiguous"));
}
