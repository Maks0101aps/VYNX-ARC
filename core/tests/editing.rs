use std::fs;
use vynx_arc_core::{
    modification::{self, Change},
    operations, preview, *,
};
#[test]
fn replacing_content_preserves_names_encryption_and_selected_preset() {
    for ext in ["zip", "7z"] {
        for password in ["", "secret"] {
            for preset in [0, 2] {
                let root = tempfile::tempdir().unwrap();
                let source = root.path().join("note.txt");
                fs::write(&source, b"original").unwrap();
                let path = root.path().join(format!("notes.{ext}"));
                operations::create(
                    &CreateOptions {
                        output: path.clone(),
                        inputs: vec![source.clone()],
                        password: password.to_string().into(),
                    },
                    &Operation::default(),
                )
                .unwrap();
                let archive = Archive::open(&path, password, &Operation::default()).unwrap();
                let expected_archive_hash =
                    operations::hash_file(&path, &Operation::default()).unwrap();
                let input = root.path().join("edit.txt");
                fs::write(&input, b"changed and verified content").unwrap();
                let expected_input_hash =
                    operations::hash_file(&input, &Operation::default()).unwrap();
                let op = Operation::default();
                op.configure(settings::Settings::from_ids(preset, 0).unwrap());
                let receipt = modification::modify_with_receipt(
                    &archive,
                    &Change::Replace {
                        name: "note.txt".into(),
                        input: input.clone(),
                        expected_archive_hash,
                        expected_input_hash,
                    },
                    password,
                    &op,
                )
                .unwrap();
                assert_eq!(
                    receipt,
                    operations::hash_file(&path, &Operation::default()).unwrap()
                );
                let updated = Archive::open(&path, password, &Operation::default()).unwrap();
                assert_eq!(updated.entries.len(), 1);
                assert_eq!(
                    preview::bytes(&updated, 0, password, &Operation::default()).unwrap(),
                    b"changed and verified content"
                );
                assert_eq!(fs::read(&input).unwrap(), b"changed and verified content");
                if !password.is_empty() {
                    assert!(
                        Archive::open(&path, "wrong", &Operation::default())
                            .and_then(|a| operations::test(&a, "wrong", &Operation::default()))
                            .is_err()
                    );
                }
                if ext == "zip" && password.is_empty() {
                    let mut z = zip::ZipArchive::new(fs::File::open(&path).unwrap()).unwrap();
                    assert_eq!(
                        z.by_index(0).unwrap().compression(),
                        if preset == 0 {
                            zip::CompressionMethod::Stored
                        } else {
                            zip::CompressionMethod::Deflated
                        }
                    );
                }
            }
        }
    }
}
#[test]
fn stale_archive_or_changing_working_copy_never_overwrites_original() {
    let root = tempfile::tempdir().unwrap();
    let source = root.path().join("note.txt");
    fs::write(&source, b"original").unwrap();
    let path = root.path().join("notes.zip");
    operations::create(
        &CreateOptions {
            output: path.clone(),
            inputs: vec![source],
            password: String::new().into(),
        },
        &Operation::default(),
    )
    .unwrap();
    let archive = Archive::open(&path, "", &Operation::default()).unwrap();
    let original = fs::read(&path).unwrap();
    let input = root.path().join("edit.txt");
    fs::write(&input, b"change").unwrap();
    let mut change = Change::Replace {
        name: "note.txt".into(),
        input: input.clone(),
        expected_archive_hash: "stale".into(),
        expected_input_hash: operations::hash_file(&input, &Operation::default()).unwrap(),
    };
    assert!(modification::modify(&archive, &change, "", &Operation::default()).is_err());
    if let Change::Replace {
        expected_archive_hash,
        expected_input_hash,
        ..
    } = &mut change
    {
        *expected_archive_hash = operations::hash_file(&path, &Operation::default()).unwrap();
        *expected_input_hash = "stale edit".into();
    }
    assert!(modification::modify(&archive, &change, "", &Operation::default()).is_err());
    let cancelled = Operation::default();
    cancelled.cancel();
    assert!(modification::modify(&archive, &change, "", &cancelled).is_err());
    assert_eq!(fs::read(&path).unwrap(), original);
    assert_eq!(fs::read(input).unwrap(), b"change");
    assert_eq!(fs::read_dir(root.path()).unwrap().count(), 3);
}
#[test]
fn selected_zip_view_checks_its_crc_without_decoding_unrelated_payloads() {
    let root = tempfile::tempdir().unwrap();
    let input = root.path().join("wanted.txt");
    fs::write(&input, b"selected").unwrap();
    let other = root.path().join("other.txt");
    let needle = b"UNRELATED-CONTENT-5631337";
    fs::write(&other, needle).unwrap();
    let path = root.path().join("select.zip");
    let op = Operation::default();
    op.configure(settings::Settings::from_ids(0, 0).unwrap());
    operations::create(
        &CreateOptions {
            output: path.clone(),
            inputs: vec![input, other],
            password: String::new().into(),
        },
        &op,
    )
    .unwrap();
    let archive = Archive::open(&path, "", &Operation::default()).unwrap();
    let mut bytes = fs::read(&path).unwrap();
    let at = bytes
        .windows(needle.len())
        .position(|w| w == needle)
        .unwrap();
    bytes[at] ^= 1;
    fs::write(&path, bytes).unwrap();
    let id = archive
        .entries
        .iter()
        .find(|e| e.name == "wanted.txt")
        .unwrap()
        .id;
    assert_eq!(
        preview::bytes(&archive, id, "", &Operation::default()).unwrap(),
        b"selected"
    );
    assert!(operations::test(&archive, "", &Operation::default()).is_err());
}
