use std::io::Cursor;
use vynx_arc_core::{Operation, iso};

fn make_synthetic_iso() -> Vec<u8> {
    let mut data = vec![0u8; 32768 + 2048 * 4];
    // Sector 16: PVD at 32768
    let pvd = &mut data[32768..32768 + 2048];
    pvd[0] = 1; // PVD
    pvd[1..6].copy_from_slice(b"CD001");
    pvd[6] = 1; // Version
    // Root directory record in PVD at byte 156
    let root = &mut pvd[156..156 + 34];
    root[0] = 34; // length
    root[2..6].copy_from_slice(&18u32.to_le_bytes()); // extent LBA 18
    root[10..14].copy_from_slice(&2048u32.to_le_bytes()); // length 2048
    root[25] = 0x02; // directory flag

    // Sector 17: SVD Joliet
    let svd = &mut data[32768 + 2048..32768 + 4096];
    svd[0] = 2; // SVD
    svd[1..6].copy_from_slice(b"CD001");
    svd[6] = 1;
    svd[88..91].copy_from_slice(b"%/@"); // Joliet escape sequence
    let svd_root = &mut svd[156..156 + 34];
    svd_root[0] = 34;
    svd_root[2..6].copy_from_slice(&18u32.to_le_bytes());
    svd_root[10..14].copy_from_slice(&2048u32.to_le_bytes());
    svd_root[25] = 0x02;

    // Sector 18: Root directory sector
    let dir = &mut data[32768 + 4096..32768 + 6144];
    // Record 1: file test.txt at LBA 19, size 5 bytes ("HELLO")
    let name_utf16: Vec<u8> = "test.txt"
        .encode_utf16()
        .flat_map(|u| u.to_be_bytes())
        .collect();
    let rec_len = (33 + name_utf16.len() + 1) & !1;
    dir[0] = rec_len as u8;
    dir[2..6].copy_from_slice(&19u32.to_le_bytes());
    dir[10..14].copy_from_slice(&5u32.to_le_bytes());
    dir[25] = 0; // regular file
    dir[32] = name_utf16.len() as u8;
    dir[33..33 + name_utf16.len()].copy_from_slice(&name_utf16);

    // Sector 19: File content "HELLO"
    let file_sector = &mut data[32768 + 6144..32768 + 8192];
    file_sector[0..5].copy_from_slice(b"HELLO");

    data
}

#[test]
fn test_iso_detect_and_parse() {
    let raw = make_synthetic_iso();
    let mut cursor = Cursor::new(raw);
    assert!(iso::detect(&mut cursor).expect("detect"));
    let op = Operation::default();
    let (entries, total_size) =
        iso::list(&mut cursor, std::path::Path::new("test.iso"), &op).expect("list");
    assert_eq!(entries.len(), 1);
    assert_eq!(entries[0].name, "test.txt");
    assert_eq!(entries[0].size, 5);
    assert_eq!(total_size, 5);
}

#[test]
fn test_iso_stream_and_read_entry() {
    let raw = make_synthetic_iso();
    let mut cursor = Cursor::new(raw);
    let op = Operation::default();
    let (entries, _) = iso::list(&mut cursor, std::path::Path::new("test.iso"), &op).expect("list");
    assert_eq!(entries.len(), 1);

    // Test read_entry
    let mut content = Vec::new();
    iso::read_entry(&mut cursor, &entries[0], &op, |_entry, data| {
        data.read_to_end(&mut content)?;
        Ok(())
    })
    .expect("read_entry");
    assert_eq!(content, b"HELLO");

    // Test stream
    let mut streamed = Vec::new();
    iso::stream(&mut cursor, &entries, &op, |entry, data| {
        let mut buf = Vec::new();
        data.read_to_end(&mut buf)?;
        streamed.push((entry.name.clone(), buf));
        Ok(())
    })
    .expect("stream");
    assert_eq!(streamed.len(), 1);
    assert_eq!(streamed[0].0, "test.txt");
    assert_eq!(streamed[0].1, b"HELLO");
}

#[test]
fn test_iso_pvd_ascii_fallback_and_version_stripping() {
    let mut data = vec![0u8; 32768 + 2048 * 4];
    // Sector 16: PVD without SVD
    let pvd = &mut data[32768..32768 + 2048];
    pvd[0] = 1; // PVD
    pvd[1..6].copy_from_slice(b"CD001");
    pvd[6] = 1;
    let root = &mut pvd[156..156 + 34];
    root[0] = 34;
    root[2..6].copy_from_slice(&18u32.to_le_bytes());
    root[10..14].copy_from_slice(&2048u32.to_le_bytes());
    root[25] = 0x02;

    // Sector 18: Root directory with FILE.TXT;1 and README.;1
    let dir = &mut data[32768 + 4096..32768 + 6144];
    // Record 1: FILE.TXT;1 at LBA 19, size 4 ("TEST")
    let name1 = b"FILE.TXT;1";
    let rec1_len = (33 + name1.len() + 1) & !1;
    dir[0] = rec1_len as u8;
    dir[2..6].copy_from_slice(&19u32.to_le_bytes());
    dir[10..14].copy_from_slice(&4u32.to_le_bytes());
    dir[32] = name1.len() as u8;
    dir[33..33 + name1.len()].copy_from_slice(name1);

    // Record 2: README.;1 at LBA 19, size 4
    let off2 = rec1_len;
    let name2 = b"README.;1";
    let rec2_len = (33 + name2.len() + 1) & !1;
    dir[off2] = rec2_len as u8;
    dir[off2 + 2..off2 + 6].copy_from_slice(&19u32.to_le_bytes());
    dir[off2 + 10..off2 + 14].copy_from_slice(&4u32.to_le_bytes());
    dir[off2 + 32] = name2.len() as u8;
    dir[off2 + 33..off2 + 33 + name2.len()].copy_from_slice(name2);

    // Sector 19: Payload "TEST"
    let file_sector = &mut data[32768 + 6144..32768 + 8192];
    file_sector[0..4].copy_from_slice(b"TEST");

    let mut cursor = Cursor::new(data);
    let op = Operation::default();
    let (entries, _) = iso::list(&mut cursor, std::path::Path::new("test.iso"), &op).expect("list");
    assert_eq!(entries.len(), 2);
    assert_eq!(entries[0].name, "FILE.TXT");
    assert_eq!(entries[1].name, "README");
}

#[test]
fn test_iso_empty_file_and_directory() {
    let mut data = vec![0u8; 32768 + 2048 * 4];
    // PVD
    let pvd = &mut data[32768..32768 + 2048];
    pvd[0] = 1;
    pvd[1..6].copy_from_slice(b"CD001");
    pvd[6] = 1;
    let root = &mut pvd[156..156 + 34];
    root[0] = 34;
    root[2..6].copy_from_slice(&18u32.to_le_bytes());
    root[10..14].copy_from_slice(&2048u32.to_le_bytes());
    root[25] = 0x02;

    // Sector 18: Dir containing empty directory "SUBDIR" and empty file "EMPTY.DAT;1"
    let dir = &mut data[32768 + 4096..32768 + 6144];
    // Subdir at LBA 19 (dir)
    let name_sub = b"SUBDIR";
    let rec1_len = (33 + name_sub.len() + 1) & !1;
    dir[0] = rec1_len as u8;
    dir[2..6].copy_from_slice(&19u32.to_le_bytes());
    dir[10..14].copy_from_slice(&2048u32.to_le_bytes());
    dir[25] = 0x02; // directory
    dir[32] = name_sub.len() as u8;
    dir[33..33 + name_sub.len()].copy_from_slice(name_sub);

    // Empty file at offset rec1_len
    let off2 = rec1_len;
    let name_file = b"EMPTY.DAT;1";
    let rec2_len = (33 + name_file.len() + 1) & !1;
    dir[off2] = rec2_len as u8;
    dir[off2 + 2..off2 + 6].copy_from_slice(&20u32.to_le_bytes());
    dir[off2 + 10..off2 + 14].copy_from_slice(&0u32.to_le_bytes()); // 0 bytes
    dir[off2 + 25] = 0x00;
    dir[off2 + 32] = name_file.len() as u8;
    dir[off2 + 33..off2 + 33 + name_file.len()].copy_from_slice(name_file);

    let mut cursor = Cursor::new(data);
    let op = Operation::default();
    let (entries, total_size) =
        iso::list(&mut cursor, std::path::Path::new("test.iso"), &op).expect("list");
    assert_eq!(entries.len(), 2);
    assert!(entries[0].directory);
    assert_eq!(entries[0].name, "SUBDIR");
    assert!(!entries[1].directory);
    assert_eq!(entries[1].name, "EMPTY.DAT");
    assert_eq!(entries[1].size, 0);
    assert_eq!(total_size, 0);

    // Stream 0-byte file without error
    let mut bytes_read = 0;
    iso::read_entry(&mut cursor, &entries[1], &op, |_entry, reader| {
        let mut buf = Vec::new();
        bytes_read = reader.read_to_end(&mut buf)?;
        Ok(())
    })
    .expect("read 0-byte entry");
    assert_eq!(bytes_read, 0);
}

#[test]
fn test_iso_directory_depth_limit_and_cycle_rejection() {
    let mut data = vec![0u8; 32768 + 2048 * 4];
    let pvd = &mut data[32768..32768 + 2048];
    pvd[0] = 1;
    pvd[1..6].copy_from_slice(b"CD001");
    pvd[6] = 1;
    let root = &mut pvd[156..156 + 34];
    root[0] = 34;
    root[2..6].copy_from_slice(&18u32.to_le_bytes());
    root[10..14].copy_from_slice(&2048u32.to_le_bytes());
    root[25] = 0x02;

    // Sector 18: Dir record pointing back to sector 18 (cycle loop)
    let dir = &mut data[32768 + 4096..32768 + 6144];
    let name = b"LOOP";
    let rec_len = (33 + name.len() + 1) & !1;
    dir[0] = rec_len as u8;
    dir[2..6].copy_from_slice(&18u32.to_le_bytes()); // points back to sector 18!
    dir[10..14].copy_from_slice(&2048u32.to_le_bytes());
    dir[25] = 0x02; // directory
    dir[32] = name.len() as u8;
    dir[33..33 + name.len()].copy_from_slice(name);

    let mut cursor = Cursor::new(data);
    let op = Operation::default();
    let err = iso::list(&mut cursor, std::path::Path::new("test.iso"), &op).unwrap_err();
    assert_eq!(err.code, "LIMIT");
    assert!(err.message.contains("maximum depth"));
}

#[test]
fn test_iso_path_traversal_attack_rejected() {
    let mut data = vec![0u8; 32768 + 2048 * 4];
    let pvd = &mut data[32768..32768 + 2048];
    pvd[0] = 1;
    pvd[1..6].copy_from_slice(b"CD001");
    pvd[6] = 1;
    let root = &mut pvd[156..156 + 34];
    root[0] = 34;
    root[2..6].copy_from_slice(&18u32.to_le_bytes());
    root[10..14].copy_from_slice(&2048u32.to_le_bytes());
    root[25] = 0x02;

    // SVD Joliet with path traversal filename "../evil.txt"
    let svd = &mut data[32768 + 2048..32768 + 4096];
    svd[0] = 2;
    svd[1..6].copy_from_slice(b"CD001");
    svd[6] = 1;
    svd[88..91].copy_from_slice(b"%/@");
    let svd_root = &mut svd[156..156 + 34];
    svd_root[0] = 34;
    svd_root[2..6].copy_from_slice(&18u32.to_le_bytes());
    svd_root[10..14].copy_from_slice(&2048u32.to_le_bytes());
    svd_root[25] = 0x02;

    let dir = &mut data[32768 + 4096..32768 + 6144];
    let name_utf16: Vec<u8> = "../evil.txt"
        .encode_utf16()
        .flat_map(|u| u.to_be_bytes())
        .collect();
    let rec_len = (33 + name_utf16.len() + 1) & !1;
    dir[0] = rec_len as u8;
    dir[2..6].copy_from_slice(&19u32.to_le_bytes());
    dir[10..14].copy_from_slice(&5u32.to_le_bytes());
    dir[25] = 0;
    dir[32] = name_utf16.len() as u8;
    dir[33..33 + name_utf16.len()].copy_from_slice(&name_utf16);

    let mut cursor = Cursor::new(data);
    let op = Operation::default();
    let err = iso::list(&mut cursor, std::path::Path::new("test.iso"), &op).unwrap_err();
    assert_eq!(err.code, "UNSAFE_PATH");
}

#[test]
fn test_iso_truncated_corrupt_files() {
    // Too small (< 32 KiB)
    let mut cursor = Cursor::new(vec![0u8; 1024]);
    assert!(!iso::detect(&mut cursor).expect("detect small"));
    let op = Operation::default();
    let err = iso::list(&mut cursor, std::path::Path::new("test.iso"), &op).unwrap_err();
    assert_eq!(err.code, "CORRUPT");

    // LBA extent extends past EOF
    let raw = make_synthetic_iso();
    // Truncate raw so sector 19 does not exist
    let truncated = raw[..32768 + 6144].to_vec();
    let mut cursor = Cursor::new(truncated);
    let err = iso::list(&mut cursor, std::path::Path::new("test.iso"), &op).unwrap_err();
    assert_eq!(err.code, "CORRUPT");
}

#[test]
fn test_udf_vrs_detection() {
    let mut data = vec![0u8; 32768 + 2048 * 2];
    let vrs = &mut data[32768..32768 + 2048];
    vrs[0] = 0;
    vrs[1..6].copy_from_slice(b"BEA01");
    let mut cursor = Cursor::new(data);
    assert!(iso::detect(&mut cursor).expect("detect UDF"));
}
