use std::{fs, path::Path};
use vynx_arc_core::{operations, preview, *};
fn create(ext: &str, password: &str, root: &Path) -> Archive {
    let input = root.join("sample.bin");
    fs::write(&input, b"\0\x01binary payload\xff").unwrap();
    let archive = root.join(format!("sample.{ext}"));
    operations::create(
        &CreateOptions {
            output: archive.clone(),
            inputs: vec![input],
            password: password.to_owned().into(),
        },
        &Operation::default(),
    )
    .unwrap();
    Archive::open(&archive, password, &Operation::default()).unwrap()
}
#[test]
fn binary_content_is_verified_for_every_writable_format_without_disk_output() {
    for ext in [
        "zip", "7z", "tar", "tar.gz", "gz", "xz", "bz2", "zst", "lzma", "tar.xz", "tar.bz2",
        "tar.zst",
    ] {
        let root = tempfile::tempdir().unwrap();
        let archive = create(ext, "", root.path());
        let before = fs::read_dir(root.path()).unwrap().count();
        assert_eq!(
            preview::bytes(&archive, archive.entries[0].id, "", &Operation::default()).unwrap(),
            b"\0\x01binary payload\xff"
        );
        assert_eq!(fs::read_dir(root.path()).unwrap().count(), before);
    }
}
#[test]
fn materialized_password_files_are_readonly_unique_and_keep_zone_marker() {
    for ext in ["zip", "7z"] {
        let root = tempfile::tempdir().unwrap();
        let archive = create(ext, "secret", root.path());
        let output = root.path().join("views");
        fs::create_dir(&output).unwrap();
        assert!(
            preview::materialize(&archive, 0, &output, "wrong", &Operation::default()).is_err()
        );
        assert_eq!(fs::read_dir(&output).unwrap().count(), 0);
        let result =
            preview::materialize(&archive, 0, &output, "secret", &Operation::default()).unwrap();
        assert_eq!(fs::read(&result).unwrap(), b"\0\x01binary payload\xff");
        assert!(result.metadata().unwrap().permissions().readonly());
        #[cfg(windows)]
        assert!(
            fs::read_to_string(format!("{}:Zone.Identifier", result.display()))
                .unwrap()
                .contains("ZoneId=3")
        );
        assert!(
            preview::materialize(&archive, 0, &output, "secret", &Operation::default()).is_err()
        );
        assert_eq!(fs::read_dir(&output).unwrap().count(), 1);
        assert_eq!(fs::read(result).unwrap(), b"\0\x01binary payload\xff");
    }
}
#[test]
fn viewer_limits_cancellation_and_unsafe_manifests_leave_no_output() {
    let root = tempfile::tempdir().unwrap();
    let mut archive = create("zip", "", root.path());
    let output = root.path().join("views");
    fs::create_dir(&output).unwrap();
    let cancelled = Operation::default();
    cancelled.cancel();
    assert!(preview::bytes(&archive, 0, "", &cancelled).is_err());
    assert!(preview::materialize(&archive, 0, &output, "", &cancelled).is_err());
    assert!(preview::bytes(&archive, 99, "", &Operation::default()).is_err());
    archive.entries[0].size = preview::MAX_CONTENT_BYTES + 1;
    assert!(preview::bytes(&archive, 0, "", &Operation::default()).is_err());
    archive.entries[0].size = preview::MAX_EXTERNAL_BYTES + 1;
    assert!(preview::materialize(&archive, 0, &output, "", &Operation::default()).is_err());
    archive.entries[0].name = "../escape.txt".into();
    assert!(preview::materialize(&archive, 0, &output, "", &Operation::default()).is_err());
    assert_eq!(fs::read_dir(&output).unwrap().count(), 0);
}
#[test]
fn corrupt_data_is_not_returned_or_published() {
    let root = tempfile::tempdir().unwrap();
    let input = root.path().join("plain.txt");
    let payload = b"Unique stored viewer integrity payload 563831";
    fs::write(&input, payload).unwrap();
    let path = root.path().join("bad.zip");
    let op = Operation::default();
    op.configure(settings::Settings::from_ids(0, 0).unwrap());
    operations::create(
        &CreateOptions {
            output: path.clone(),
            inputs: vec![input],
            password: String::new().into(),
        },
        &op,
    )
    .unwrap();
    let archive = Archive::open(&path, "", &Operation::default()).unwrap();
    let mut data = fs::read(&path).unwrap();
    let offset = data
        .windows(payload.len())
        .position(|w| w == payload)
        .unwrap();
    data[offset] ^= 1;
    fs::write(&path, data).unwrap();
    let output = root.path().join("views");
    fs::create_dir(&output).unwrap();
    assert!(preview::bytes(&archive, 0, "", &Operation::default()).is_err());
    assert!(preview::materialize(&archive, 0, &output, "", &Operation::default()).is_err());
    assert_eq!(fs::read_dir(output).unwrap().count(), 0);
}
