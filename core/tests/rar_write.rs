#![cfg(windows)]
use std::{fs, path::Path};
use vynx_arc_core::{
    Archive, CreateOptions, Operation, operations, rar_write::Options, settings::Settings,
};

fn create(
    root: &Path,
    preset: u8,
    rar: Options,
    password: &str,
    split: Option<u64>,
) -> std::path::PathBuf {
    let input = root.join("дані.txt");
    fs::write(
        &input,
        b"Native independent RAR5 compression.\n".repeat(4096),
    )
    .unwrap();
    let output = root.join(format!("out-{preset}.rar"));
    let options = CreateOptions {
        output: output.clone(),
        inputs: vec![input],
        password: password.to_owned().into(),
    };
    let op = Operation::default();
    op.configure(Settings::from_ids(preset, 1).unwrap());
    op.configure_rar(rar).unwrap();
    if let Some(size) = split {
        operations::create_split(&options, size, &op).unwrap();
    } else {
        operations::create(&options, &op).unwrap();
    }
    if split.is_some() {
        root.join(format!("out-{preset}.part1.rar"))
    } else {
        output
    }
}

#[test]
fn all_presets_are_independently_decodable_and_really_compress() {
    let root = tempfile::tempdir().unwrap();
    let mut sizes = Vec::new();
    for preset in 0..=3 {
        let path = create(root.path(), preset, Options::default(), "", None);
        assert!(fs::read(&path).unwrap().starts_with(b"Rar!\x1a\x07\x01\0"));
        let op = Operation::default();
        let archive = Archive::open(&path, "", &op).unwrap();
        operations::test(&archive, "", &op).unwrap();
        let hashes = vynx_arc_core::hashing::entries(&archive, &[0], "", &op).unwrap();
        let expected = operations::hash_file(&root.path().join("дані.txt"), &op).unwrap();
        assert!(expected.contains(&hashes[0].sha256));
        sizes.push(fs::metadata(path).unwrap().len());
    }
    for size in &sizes[1..] {
        assert!(*size < sizes[0] / 4, "{sizes:?}");
    }
}

#[test]
fn solid_encryption_headers_recovery_and_volumes() {
    for (solid, headers, recovery, split) in [
        (true, false, 0, None),
        (false, true, 0, None),
        (true, true, 3, None),
        (false, false, 0, Some(65536)),
        (true, true, 0, Some(65536)),
    ] {
        let root = tempfile::tempdir().unwrap();
        let path = create(
            root.path(),
            1,
            Options {
                solid,
                encrypt_headers: headers,
                recovery_percent: recovery,
                dictionary: 1 << 20,
            },
            "test-password",
            split,
        );
        let op = Operation::default();
        let archive = Archive::open(&path, "test-password", &op).unwrap();
        operations::test(&archive, "test-password", &op).unwrap();
        if headers {
            assert!(Archive::open(&path, "wrong", &op).is_err());
        } else {
            assert!(
                operations::test(&Archive::open(&path, "", &op).unwrap(), "wrong", &op).is_err()
            );
        }
    }
}

#[test]
fn rebuild_add_delete_rename_and_failure_preserves_original() {
    use vynx_arc_core::modification::{Change, modify};
    let root = tempfile::tempdir().unwrap();
    let path = create(
        root.path(),
        1,
        Options {
            solid: true,
            encrypt_headers: true,
            recovery_percent: 3,
            ..Default::default()
        },
        "pw",
        None,
    );
    let op = Operation::default();
    let added = root.path().join("new.txt");
    fs::write(&added, b"added bytes").unwrap();
    for change in [
        Change::Add {
            inputs: vec![added],
            folder: String::new(),
        },
        Change::Rename {
            old: "new.txt".into(),
            new: "renamed.txt".into(),
        },
        Change::Delete {
            names: vec!["дані.txt".into()],
        },
    ] {
        let archive = Archive::open(&path, "pw", &op).unwrap();
        modify(&archive, &change, "pw", &op).unwrap();
    }
    let archive = Archive::open(&path, "pw", &op).unwrap();
    assert_eq!(archive.entries.len(), 1);
    assert_eq!(archive.entries[0].name, "renamed.txt");
    operations::test(&archive, "pw", &op).unwrap();
    assert!(Archive::open(&path, "", &op).is_err());
    let before = fs::read(&path).unwrap();
    op.cancel();
    assert_eq!(
        modify(
            &archive,
            &Change::Delete {
                names: vec!["renamed.txt".into()]
            },
            "pw",
            &op
        )
        .unwrap_err()
        .code,
        "CANCELLED"
    );
    assert_eq!(fs::read(path).unwrap(), before);
}

#[test]
fn invalid_options_and_cancelled_creation_publish_nothing() {
    let root = tempfile::tempdir().unwrap();
    let input = root.path().join("input.txt");
    fs::write(&input, b"input").unwrap();
    let output = root.path().join("output.rar");
    let op = Operation::default();
    for dictionary in [1, 129 << 10, 64 << 20, u64::MAX] {
        assert_eq!(
            op.configure_rar(Options {
                dictionary,
                ..Default::default()
            })
            .unwrap_err()
            .code,
            "OPTIONS"
        );
    }
    assert!(
        op.configure_rar(Options {
            recovery_percent: 11,
            ..Default::default()
        })
        .is_err()
    );
    op.cancel();
    assert_eq!(
        operations::create(
            &CreateOptions {
                output: output.clone(),
                inputs: vec![input.clone()],
                password: String::new().into()
            },
            &op
        )
        .unwrap_err()
        .code,
        "CANCELLED"
    );
    assert!(!output.exists());
    assert_eq!(fs::read(input).unwrap(), b"input");
}

#[test]
fn actual_multiple_volumes_missing_part_conflict_and_readonly_rebuild() {
    use vynx_arc_core::modification::{Change, modify};
    let root = tempfile::tempdir().unwrap();
    let input = root.path().join("random.bin");
    let mut seed = 90u64;
    let bytes: Vec<_> = (0..300_000)
        .map(|_| {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            seed as u8
        })
        .collect();
    fs::write(&input, &bytes).unwrap();
    let options = CreateOptions {
        output: root.path().join("split.rar"),
        inputs: vec![input],
        password: "secret".to_string().into(),
    };
    let op = Operation::default();
    op.configure_rar(Options {
        solid: true,
        encrypt_headers: true,
        ..Default::default()
    })
    .unwrap();
    operations::create_split(&options, 65536, &op).unwrap();
    let first = root.path().join("split.part1.rar");
    let second = root.path().join("split.part2.rar");
    assert!(second.exists());
    let archive = Archive::open(&first, "secret", &op).unwrap();
    assert!(!archive.writable);
    assert_eq!(
        modify(
            &archive,
            &Change::Delete {
                names: vec!["random.bin".into()]
            },
            "secret",
            &op
        )
        .unwrap_err()
        .code,
        "READ_ONLY"
    );
    let held = second.with_extension("held");
    fs::rename(&second, &held).unwrap();
    let error = operations::test(&archive, "secret", &op).unwrap_err();
    assert_eq!(error.code, "MISSING_VOLUME");
    fs::rename(held, &second).unwrap();
    let before = fs::read(&first).unwrap();
    assert_eq!(
        operations::create_split(&options, 65536, &op)
            .unwrap_err()
            .code,
        "CONFLICT"
    );
    assert_eq!(fs::read(&first).unwrap(), before);
}

#[test]
fn cancellation_during_native_compression_never_publishes() {
    let root = tempfile::tempdir().unwrap();
    let input = root.path().join("large.bin");
    fs::write(
        &input,
        (0..8_000_000u64)
            .map(|i| ((i * 17) ^ (i >> 9)) as u8)
            .collect::<Vec<_>>(),
    )
    .unwrap();
    let output = root.path().join("cancel.rar");
    let options = CreateOptions {
        output: output.clone(),
        inputs: vec![input.clone()],
        password: String::new().into(),
    };
    let op = Operation::default();
    let other = op.clone();
    let worker = std::thread::spawn(move || operations::create(&options, &other));
    let start = std::time::Instant::now();
    while !worker.is_finished() && start.elapsed() < std::time::Duration::from_secs(15) {
        if op.snapshot().phase == "Compressing RAR5 files" {
            op.cancel();
            break;
        }
        std::thread::sleep(std::time::Duration::from_millis(1));
    }
    assert_eq!(worker.join().unwrap().unwrap_err().code, "CANCELLED");
    assert!(!output.exists());
    assert_eq!(fs::metadata(input).unwrap().len(), 8_000_000);
    assert_eq!(fs::read_dir(root.path()).unwrap().count(), 1);
}

#[test]
fn locked_rar_rebuild_preserves_source_bytes() {
    use std::os::windows::fs::OpenOptionsExt;
    use vynx_arc_core::modification::{Change, modify};
    let root = tempfile::tempdir().unwrap();
    let path = create(root.path(), 1, Options::default(), "", None);
    let op = Operation::default();
    let archive = Archive::open(&path, "", &op).unwrap();
    let before = fs::read(&path).unwrap();
    let _guard = fs::OpenOptions::new()
        .read(true)
        .share_mode(1)
        .open(&path)
        .unwrap();
    assert!(
        modify(
            &archive,
            &Change::Rename {
                old: archive.entries[0].name.clone(),
                new: "renamed.txt".into()
            },
            "",
            &op
        )
        .is_err()
    );
    assert_eq!(fs::read(&path).unwrap(), before);
}

#[test]
fn nested_rar_paths_and_working_copy_saveback_use_canonical_names() {
    use vynx_arc_core::modification::{Change, modify};
    let root = tempfile::tempdir().unwrap();
    let folder = root.path().join("folder");
    fs::create_dir(&folder).unwrap();
    fs::write(folder.join("child.txt"), b"before").unwrap();
    let path = root.path().join("tree.rar");
    let op = Operation::default();
    operations::create(
        &CreateOptions {
            output: path.clone(),
            inputs: vec![folder],
            password: "pw".to_owned().into(),
        },
        &op,
    )
    .unwrap();
    let archive = Archive::open(&path, "pw", &op).unwrap();
    assert!(archive.entries.iter().any(|e| e.name == "folder/child.txt"));
    let edited = root.path().join("edited.txt");
    fs::write(&edited, b"after").unwrap();
    modify(
        &archive,
        &Change::Replace {
            name: "folder/child.txt".into(),
            input: edited.clone(),
            expected_archive_hash: operations::hash_file(&path, &op).unwrap(),
            expected_input_hash: operations::hash_file(&edited, &op).unwrap(),
        },
        "pw",
        &op,
    )
    .unwrap();
    let archive = Archive::open(&path, "pw", &op).unwrap();
    let child = archive
        .entries
        .iter()
        .find(|e| e.name == "folder/child.txt")
        .unwrap();
    let hashes = vynx_arc_core::hashing::entries(&archive, &[child.id], "pw", &op).unwrap();
    assert!(
        operations::hash_file(&edited, &op)
            .unwrap()
            .contains(&hashes[0].sha256)
    );
}
