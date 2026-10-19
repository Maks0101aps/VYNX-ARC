use std::{fs, path::Path};
use vynx_arc_core::{
    conversion::{self, Options, Replacement},
    hashing, operations, *,
};
fn hashes(a: &Archive, pw: &str) -> Vec<(String, String)> {
    let mut h: Vec<_> = hashing::entries(a, &[], pw, &Operation::default())
        .unwrap()
        .into_iter()
        .map(|h| (h.name, h.sha256))
        .collect();
    h.sort();
    h
}
#[test]
fn conversion_roundtrips_every_readable_writer_with_directories_and_encryption() {
    let dir = tempfile::tempdir().unwrap();
    let input = dir.path().join("tree");
    fs::create_dir_all(input.join("empty")).unwrap();
    fs::write(input.join("note.txt"), b"conversion bytes").unwrap();
    for source in ["zip", "7z", "tar", "tar.gz", "tar.xz", "tar.bz2", "tar.zst"] {
        for source_password in ["", "source-password"] {
            if !source_password.is_empty() && !["zip", "7z"].contains(&source) {
                continue;
            }
            let path = dir
                .path()
                .join(format!("source-{}.{source}", source_password.len()));
            operations::create(
                &CreateOptions {
                    output: path.clone(),
                    inputs: vec![input.clone()],
                    password: source_password.to_owned().into(),
                },
                &Operation::default(),
            )
            .unwrap();
            let before = fs::read(&path).unwrap();
            let a = Archive::open(&path, source_password, &Operation::default()).unwrap();
            let expected = hashes(&a, source_password);
            for (format, ext) in [(Format::Zip, "zip"), (Format::SevenZ, "7z")] {
                for dest_password in ["", "destination-password"] {
                    let output = dir.path().join(format!(
                        "out-{source}-{}-{}.{ext}",
                        source_password.len(),
                        dest_password.len()
                    ));
                    conversion::convert(
                        &a,
                        &Options {
                            output: output.clone(),
                            format,
                            password: dest_password.to_owned().into(),
                            allow_decrypt: true,
                            replacement: None,
                        },
                        source_password,
                        &Operation::default(),
                    )
                    .unwrap();
                    let result =
                        Archive::open(&output, dest_password, &Operation::default()).unwrap();
                    assert_eq!(hashes(&result, dest_password), expected);
                    assert_eq!(result.entries.len(), a.entries.len());
                    if !dest_password.is_empty() {
                        assert!(
                            Archive::open(&output, "wrong", &Operation::default())
                                .and_then(|a| operations::test(&a, "wrong", &Operation::default()))
                                .is_err()
                        );
                    }
                    assert_eq!(fs::read(&path).unwrap(), before);
                }
            }
        }
    }
}
#[test]
fn save_as_replaces_verified_content_without_touching_original_or_working_copy() {
    let dir = tempfile::tempdir().unwrap();
    let input = dir.path().join("note.txt");
    fs::write(&input, b"original").unwrap();
    let source = dir.path().join("source.zip");
    operations::create(
        &CreateOptions {
            output: source.clone(),
            inputs: vec![input.clone()],
            password: String::new().into(),
        },
        &Operation::default(),
    )
    .unwrap();
    let a = Archive::open(&source, "", &Operation::default()).unwrap();
    let before = fs::read(&source).unwrap();
    fs::write(&input, b"edited with atomic-save semantics").unwrap();
    for (format, ext) in [(Format::Zip, "zip"), (Format::SevenZ, "7z")] {
        let output = dir.path().join(format!("saved.{ext}"));
        let opts = Options {
            output: output.clone(),
            format,
            password: "destination-password".to_owned().into(),
            allow_decrypt: false,
            replacement: Some(Replacement {
                name: "note.txt".into(),
                input: input.clone(),
                archive_hash: operations::hash_file(&source, &Operation::default()).unwrap(),
                input_hash: operations::hash_file(&input, &Operation::default()).unwrap(),
            }),
        };
        conversion::convert(&a, &opts, "", &Operation::default()).unwrap();
        let result = Archive::open(&output, "destination-password", &Operation::default()).unwrap();
        assert_eq!(
            preview::bytes(&result, 0, "destination-password", &Operation::default()).unwrap(),
            b"edited with atomic-save semantics"
        );
        assert_eq!(fs::read(&source).unwrap(), before);
    }
    assert_eq!(
        fs::read(&input).unwrap(),
        b"edited with atomic-save semantics"
    );
}
#[test]
fn conversion_refuses_implicit_decryption_collision_stale_edits_and_cancellation() {
    let dir = tempfile::tempdir().unwrap();
    let input = dir.path().join("note.txt");
    fs::write(&input, b"original").unwrap();
    let source = dir.path().join("source.zip");
    operations::create(
        &CreateOptions {
            output: source.clone(),
            inputs: vec![input.clone()],
            password: "password".to_owned().into(),
        },
        &Operation::default(),
    )
    .unwrap();
    let a = Archive::open(&source, "password", &Operation::default()).unwrap();
    let output = dir.path().join("result.zip");
    let mut opts = Options {
        output: output.clone(),
        format: Format::Zip,
        password: String::new().into(),
        allow_decrypt: false,
        replacement: None,
    };
    assert_eq!(
        conversion::convert(&a, &opts, "password", &Operation::default())
            .unwrap_err()
            .code,
        "ENCRYPTION"
    );
    assert!(!output.exists());
    opts.allow_decrypt = true;
    let cancelled = Operation::default();
    cancelled.cancel();
    assert_eq!(
        conversion::convert(&a, &opts, "password", &cancelled)
            .unwrap_err()
            .code,
        "CANCELLED"
    );
    assert!(!output.exists());
    opts.replacement = Some(Replacement {
        name: "note.txt".into(),
        input: input.clone(),
        archive_hash: "stale".into(),
        input_hash: "stale".into(),
    });
    assert_eq!(
        conversion::convert(&a, &opts, "password", &Operation::default())
            .unwrap_err()
            .code,
        "CHANGED"
    );
    assert!(!output.exists());
    opts.replacement = None;
    fs::write(&output, b"existing destination").unwrap();
    assert_eq!(
        conversion::convert(&a, &opts, "password", &Operation::default())
            .unwrap_err()
            .code,
        "CONFLICT"
    );
    assert_eq!(fs::read(&output).unwrap(), b"existing destination");
}
#[cfg(windows)]
#[test]
fn converts_real_rar4_rar5_fixtures() {
    let dir = tempfile::tempdir().unwrap();
    for name in [
        "test_read_format_rar5_stored.rar",
        "test_read_format_rar5_compressed.rar",
        "test_read_format_rar_binary_data.rar",
    ] {
        let path = Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../tests/archives")
            .join(name);
        let before = fs::read(&path).unwrap();
        let a = Archive::open(&path, "", &Operation::default()).unwrap();
        for (format, ext) in [(Format::Zip, "zip"), (Format::SevenZ, "7z")] {
            let output = dir.path().join(format!("{name}.{ext}"));
            conversion::convert(
                &a,
                &Options {
                    output: output.clone(),
                    format,
                    password: String::new().into(),
                    allow_decrypt: false,
                    replacement: None,
                },
                "",
                &Operation::default(),
            )
            .unwrap();
            let result = Archive::open(&output, "", &Operation::default()).unwrap();
            assert_eq!(hashes(&result, ""), hashes(&a, ""));
            assert_eq!(fs::read(&path).unwrap(), before);
        }
    }
}

#[test]
fn conversion_cancellation_during_streaming_keeps_source_and_removes_staging() {
    let dir = tempfile::tempdir().unwrap();
    let input = dir.path().join("large.bin");
    fs::write(&input, vec![b'x'; 32 << 20]).unwrap();
    let source = dir.path().join("source.zip");
    let create = Operation::default();
    create.configure(settings::Settings::from_ids(0, 0).unwrap());
    operations::create(
        &CreateOptions {
            output: source.clone(),
            inputs: vec![input],
            password: String::new().into(),
        },
        &create,
    )
    .unwrap();
    let original = operations::hash_file(&source, &Operation::default()).unwrap();
    let archive = Archive::open(&source, "", &Operation::default()).unwrap();
    let output = dir.path().join("result.7z");
    let op = Operation::default();
    let cancellation = op.clone();
    let stop = std::sync::Arc::new(std::sync::atomic::AtomicBool::new(false));
    let finished = stop.clone();
    let watcher = std::thread::spawn(move || {
        while !finished.load(std::sync::atomic::Ordering::Acquire) {
            if cancellation.snapshot().done > 0 {
                cancellation.cancel();
                return true;
            }
            std::thread::sleep(std::time::Duration::from_millis(1));
        }
        false
    });
    let result = conversion::convert(
        &archive,
        &Options {
            output: output.clone(),
            format: Format::SevenZ,
            password: String::new().into(),
            allow_decrypt: false,
            replacement: None,
        },
        "",
        &op,
    );
    stop.store(true, std::sync::atomic::Ordering::Release);
    assert!(
        watcher.join().unwrap(),
        "cancellation must occur after work starts"
    );
    assert_eq!(result.unwrap_err().code, "CANCELLED");
    assert!(!output.exists());
    assert_eq!(
        operations::hash_file(&source, &Operation::default()).unwrap(),
        original
    );
    assert_eq!(
        fs::read_dir(dir.path()).unwrap().count(),
        2,
        "staged output must be removed"
    );
}
