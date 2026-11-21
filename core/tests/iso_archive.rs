use std::{fs, path::Path};
use tempfile::tempdir;
use vynx_arc_core::{Archive, Format, Operation, preview};

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
    let name_utf16: Vec<u8> = "test.txt".encode_utf16().flat_map(|u| u.to_be_bytes()).collect();
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
fn test_iso_archive_pipeline() {
    let t = tempdir().unwrap();
    let iso_path = t.path().join("sample.iso");
    fs::write(&iso_path, make_synthetic_iso()).unwrap();

    let op = Operation::default();
    let archive = Archive::open(&iso_path, "", &op).expect("Archive::open");

    // 1. Verify format & writable flag
    assert_eq!(archive.format, Format::Iso);
    assert_eq!(archive.format.name(), "ISO");
    assert!(!archive.writable, "ISO archive must be read-only");
    assert_eq!(archive.entries.len(), 1);
    assert_eq!(archive.entries[0].name, "test.txt");
    assert_eq!(archive.entries[0].size, 5);

    // 2. Verify streams()
    let mut streamed = Vec::new();
    archive
        .streams("", &op, |entry, reader| {
            let mut buf = Vec::new();
            reader.read_to_end(&mut buf)?;
            streamed.push((entry.name.clone(), buf));
            Ok(())
        })
        .expect("Archive::streams");
    assert_eq!(streamed.len(), 1);
    assert_eq!(streamed[0].0, "test.txt");
    assert_eq!(streamed[0].1, b"HELLO");

    // 3. Verify direct iso_entry()
    let mut entry_bytes = Vec::new();
    archive
        .iso_entry(archive.entries[0].id, &op, |_entry, reader| {
            reader.read_to_end(&mut entry_bytes)?;
            Ok(())
        })
        .expect("Archive::iso_entry");
    assert_eq!(entry_bytes, b"HELLO");

    // 4. Verify preview::bytes()
    let preview_bytes =
        preview::bytes(&archive, archive.entries[0].id, "", &op).expect("preview::bytes");
    assert_eq!(preview_bytes, b"HELLO");

    // 5. Verify preview::text()
    let preview_text =
        preview::text(&archive, archive.entries[0].id, "", &op).expect("preview::text");
    assert_eq!(preview_text, "HELLO");
}

#[test]
fn test_iso_archive_read_only_constraints() {
    let t = tempdir().unwrap();
    let iso_path = t.path().join("sample.iso");
    fs::write(&iso_path, make_synthetic_iso()).unwrap();

    let op = Operation::default();
    let archive = Archive::open(&iso_path, "", &op).expect("Archive::open");

    // Creation or output to ISO is rejected
    assert!(Format::for_output(Path::new("output.iso")).is_err());

    // Modification is rejected
    use vynx_arc_core::modification::{Change, modify};
    let change = Change::Delete {
        names: vec!["test.txt".into()],
    };
    let err = modify(&archive, &change, "", &op).unwrap_err();
    assert_eq!(err.code, "READ_ONLY");
}
