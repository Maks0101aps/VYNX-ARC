use std::{fs, path::Path};
use tempfile::tempdir;
use vynx_arc_core::{Archive, Format, Operation, ffi};

fn make_multi_image_wim() -> Vec<u8> {
    let mut data = vec![0u8; 1024];
    data[0..8].copy_from_slice(b"MSWIM\0\0\0");
    data[8..12].copy_from_slice(&208u32.to_le_bytes());
    data[12..16].copy_from_slice(&0x00010d00u32.to_le_bytes());
    data[16..20].copy_from_slice(&0x00020002u32.to_le_bytes());
    data[40..42].copy_from_slice(&1u16.to_le_bytes());
    data[42..44].copy_from_slice(&1u16.to_le_bytes());
    data[44..48].copy_from_slice(&2u32.to_le_bytes());

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
fn test_wim_archive_pipeline() {
    let t = tempdir().unwrap();
    let wim_path = t.path().join("sample.wim");
    fs::write(&wim_path, make_multi_image_wim()).unwrap();

    let op = Operation::default();
    let archive = Archive::open(&wim_path, "", &op).expect("Archive::open");

    // 1. Verify format & writable flag
    assert_eq!(archive.format, Format::Wim);
    assert_eq!(archive.format.name(), "WIM");
    assert!(!archive.writable, "WIM archive must be read-only");
    assert_eq!(archive.entries.len(), 2);
    assert_eq!(archive.entries[0].name, "[1] Windows 11 Home");
    assert_eq!(archive.entries[0].size, 4000);
    assert_eq!(archive.entries[1].name, "[2] Windows 11 Pro");
    assert_eq!(archive.entries[1].size, 6000);

    // 2. Verify streams()
    let mut streamed = 0;
    archive
        .streams("", &op, |_entry, _reader| {
            streamed += 1;
            Ok(())
        })
        .expect("Archive::streams");
    // All image entries are directories, so non-directory stream count is 0
    assert_eq!(streamed, 0);

    // 3. Verify FFI properties
    assert_eq!(ffi::archive_format(&archive), "WIM");
    assert!(!ffi::archive_writable(&archive));
}

#[test]
fn test_wim_archive_read_only_constraints() {
    let t = tempdir().unwrap();
    let wim_path = t.path().join("sample.wim");
    fs::write(&wim_path, make_multi_image_wim()).unwrap();

    let op = Operation::default();
    let archive = Archive::open(&wim_path, "", &op).expect("Archive::open");

    // Creation or output to WIM/SWM/ESD is rejected
    assert!(Format::for_output(Path::new("output.wim")).is_err());
    assert!(Format::for_output(Path::new("output.swm")).is_err());
    assert!(Format::for_output(Path::new("output.esd")).is_err());

    // Format 14 should reject compression details & creation
    assert!(ffi::compression_details(14, 0, 0).is_err());
    assert!(ffi::create_archive_as("out.wim", &[], 14, "", &op).is_err());

    // Modification is rejected
    use vynx_arc_core::modification::{Change, modify};
    let change = Change::Delete {
        names: vec!["[1] Windows 11 Home".into()],
    };
    let err = modify(&archive, &change, "", &op).unwrap_err();
    assert_eq!(err.code, "READ_ONLY");
}

#[test]
#[cfg(windows)]
fn test_wim_native_api_loading() {
    let api = vynx_arc_core::wim_native::WimApi::load();
    assert!(api.is_ok(), "wimgapi.dll should load on Windows 10/11: {:?}", api.err());
}

#[test]
fn test_wim_extract_image_error_handling() {
    let t = tempdir().unwrap();
    let dest = t.path().join("out");
    let non_existent = t.path().join("missing.wim");
    let op = Operation::default();

    // 1. Missing WIM file returns error
    let res = vynx_arc_core::wim_native::extract_image(&non_existent, 1, &dest, &op);
    assert!(res.is_err());

    // 2. Cancelled operation returns error
    op.cancel();
    let res = vynx_arc_core::wim_native::extract_image(&non_existent, 1, &dest, &op);
    assert!(res.is_err());
    assert_eq!(res.unwrap_err().code, "CANCELLED");
}
