use std::{fs, path::Path};
use vynx_arc_core::{
    modification::{self, Change},
    operations, *,
};

fn make(dir: &Path, ext: &str, password: &str) -> Archive {
    let folder = dir.join("source");
    fs::create_dir_all(folder.join("nested")).unwrap();
    fs::write(folder.join("nested/дані.txt"), b"original content").unwrap();
    fs::write(folder.join("keep.txt"), b"keep").unwrap();
    let output = dir.join(format!("archive.{ext}"));
    operations::create(
        &CreateOptions {
            output: output.clone(),
            inputs: vec![folder],
            password: password.to_string().into(),
        },
        &Operation::default(),
    )
    .unwrap();
    Archive::open(&output, password, &Operation::default()).unwrap()
}
fn contents(a: &Archive, password: &str) -> std::collections::HashMap<String, Vec<u8>> {
    let mut result = std::collections::HashMap::new();
    a.streams(password, &Operation::default(), |e, r| {
        let mut bytes = Vec::new();
        r.read_to_end(&mut bytes)?;
        result.insert(e.name.clone(), bytes);
        Ok(())
    })
    .unwrap();
    result
}
#[test]
fn add_rename_delete_unicode_and_encryption() {
    for ext in ["zip", "7z"] {
        for password in ["", "private"] {
            let t = tempfile::tempdir().unwrap();
            let mut a = make(t.path(), ext, password);
            let added = t.path().join("нове.txt");
            fs::write(&added, b"new bytes").unwrap();
            modification::modify(
                &a,
                &Change::Add {
                    inputs: vec![added],
                    folder: "source/nested".into(),
                },
                password,
                &Operation::default(),
            )
            .unwrap();
            a = Archive::open(&a.path, password, &Operation::default()).unwrap();
            assert_eq!(
                contents(&a, password)["source/nested/нове.txt"],
                b"new bytes"
            );
            modification::modify(
                &a,
                &Change::Rename {
                    old: "source/nested".into(),
                    new: "source/перейменовано".into(),
                },
                password,
                &Operation::default(),
            )
            .unwrap();
            a = Archive::open(&a.path, password, &Operation::default()).unwrap();
            assert_eq!(
                contents(&a, password)["source/перейменовано/дані.txt"],
                b"original content"
            );
            modification::modify(
                &a,
                &Change::Delete {
                    names: vec!["source/перейменовано".into()],
                },
                password,
                &Operation::default(),
            )
            .unwrap();
            a = Archive::open(&a.path, password, &Operation::default()).unwrap();
            assert_eq!(contents(&a, password).len(), 1);
            assert_eq!(contents(&a, password)["source/keep.txt"], b"keep");
            if !password.is_empty() {
                assert!(
                    a.entries
                        .iter()
                        .filter(|e| !e.directory)
                        .all(|e| e.encrypted)
                );
            }
        }
    }
}
#[test]
fn duplicate_invalid_rename_and_wrong_password_preserve_original() {
    for ext in ["zip", "7z"] {
        let t = tempfile::tempdir().unwrap();
        let a = make(t.path(), ext, "private");
        let before = fs::read(&a.path).unwrap();
        for change in [
            Change::Rename {
                old: "source/keep.txt".into(),
                new: "source/nested/дані.txt".into(),
            },
            Change::Rename {
                old: "source/keep.txt".into(),
                new: "../escape".into(),
            },
            Change::Add {
                inputs: vec![t.path().join("source")],
                folder: String::new(),
            },
        ] {
            assert!(modification::modify(&a, &change, "private", &Operation::default()).is_err());
            assert_eq!(fs::read(&a.path).unwrap(), before);
        }
        assert!(
            modification::modify(
                &a,
                &Change::Delete {
                    names: vec!["source/keep.txt".into()]
                },
                "wrong",
                &Operation::default()
            )
            .is_err()
        );
        assert_eq!(fs::read(&a.path).unwrap(), before);
    }
}
#[test]
fn cancelled_rebuild_leaves_original_and_no_staging_files() {
    for ext in ["zip", "7z"] {
        let t = tempfile::tempdir().unwrap();
        let a = make(t.path(), ext, "");
        let before = fs::read(&a.path).unwrap();
        let added = t.path().join("large.bin");
        fs::write(&added, vec![0x63; 16 * 1024 * 1024]).unwrap();
        let op = Operation::default();
        let watch = op.clone();
        let cancel = std::thread::spawn(move || {
            let deadline = std::time::Instant::now() + std::time::Duration::from_secs(10);
            while watch.snapshot().done < 1024 && std::time::Instant::now() < deadline {
                std::thread::yield_now();
            }
            watch.cancel();
        });
        assert!(
            modification::modify(
                &a,
                &Change::Add {
                    inputs: vec![added],
                    folder: String::new()
                },
                "",
                &op
            )
            .is_err()
        );
        cancel.join().unwrap();
        assert_eq!(fs::read(&a.path).unwrap(), before);
        assert!(
            fs::read_dir(t.path()).unwrap().all(|p| !p
                .unwrap()
                .file_name()
                .to_string_lossy()
                .starts_with(".tmp"))
        );
    }
}
#[test]
fn explicit_format_rejects_misleading_extension() {
    let t = tempfile::tempdir().unwrap();
    let input = t.path().join("a");
    fs::write(&input, b"data").unwrap();
    let output = t.path().join("misleading.zip");
    assert!(
        operations::create_as(
            &CreateOptions {
                output: output.clone(),
                inputs: vec![input],
                password: String::new().into()
            },
            Format::SevenZ,
            &Operation::default()
        )
        .is_err()
    );
    assert!(!output.exists());
}
#[cfg(windows)]
#[test]
fn locked_original_rejects_replacement_without_data_loss() {
    use std::os::windows::fs::OpenOptionsExt;
    let t = tempfile::tempdir().unwrap();
    let a = make(t.path(), "zip", "");
    let before = fs::read(&a.path).unwrap();
    let _lock = fs::OpenOptions::new()
        .read(true)
        .share_mode(1)
        .open(&a.path)
        .unwrap();
    let error = modification::modify(
        &a,
        &Change::Delete {
            names: vec!["source/keep.txt".into()],
        },
        "",
        &Operation::default(),
    )
    .unwrap_err();
    assert_eq!(error.code, "REPLACE");
    assert_eq!(fs::read(&a.path).unwrap(), before);
}
#[test]
fn deletion_can_produce_empty_archive() {
    for ext in ["zip", "7z"] {
        let t = tempfile::tempdir().unwrap();
        let a = make(t.path(), ext, "");
        modification::modify(
            &a,
            &Change::Delete {
                names: vec!["source".into()],
            },
            "",
            &Operation::default(),
        )
        .unwrap();
        assert!(
            Archive::open(&a.path, "", &Operation::default())
                .unwrap()
                .entries
                .is_empty()
        );
    }
}

#[test]
fn long_archive_path_rebuild_and_corrupt_input_preserve_bytes() {
    let t = tempfile::tempdir().unwrap();
    let mut a = make(t.path(), "zip", "");
    let long = format!("{}/{}/дані.txt", "a".repeat(180), "b".repeat(180));
    modification::modify(
        &a,
        &Change::Rename {
            old: "source/nested/дані.txt".into(),
            new: long.clone(),
        },
        "",
        &Operation::default(),
    )
    .unwrap();
    a = Archive::open(&a.path, "", &Operation::default()).unwrap();
    assert_eq!(contents(&a, "")[&long], b"original content");
    let mut bytes = fs::read(&a.path).unwrap();
    let mut z = zip::ZipArchive::new(fs::File::open(&a.path).unwrap()).unwrap();
    let start = z.by_name("source/keep.txt").unwrap().data_start().unwrap() as usize;
    drop(z);
    bytes[start] ^= 0x40;
    fs::write(&a.path, &bytes).unwrap();
    let stale = Archive::open(&a.path, "", &Operation::default()).unwrap();
    assert!(
        modification::modify(
            &stale,
            &Change::Delete { names: vec![long] },
            "",
            &Operation::default()
        )
        .is_err()
    );
    assert_eq!(fs::read(&a.path).unwrap(), bytes);
}
