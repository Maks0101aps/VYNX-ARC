use std::{
    io::Write,
    path::PathBuf,
    process::{Command, Stdio},
    time::Duration,
};
use vynx_arc_core::{CreateOptions, Operation, operations, worker::Client};
fn client() -> Client {
    Client::at(
        PathBuf::from(env!("CARGO_BIN_EXE_vynxarc-worker")),
        Duration::from_secs(15),
    )
}
#[test]
#[cfg(windows)]
fn native_rar_options_cross_worker_boundary_and_preserve_encryption() {
    let root = tempfile::tempdir().unwrap();
    let input = root.path().join("hello.txt");
    std::fs::write(&input, b"worker RAR input".repeat(2000)).unwrap();
    let output = root.path().join("worker.rar");
    let op = Operation::default();
    op.configure_rar(vynx_arc_core::rar_write::Options {
        solid: true,
        dictionary: 1 << 20,
        encrypt_headers: true,
        recovery_percent: 3,
    })
    .unwrap();
    client()
        .create(
            &CreateOptions {
                output: output.clone(),
                inputs: vec![input],
                password: "ipc-rar-secret".to_owned().into(),
            },
            vynx_arc_core::Format::Rar,
            0,
            &op,
        )
        .unwrap();
    assert!(client().open(&output, "wrong", &op).is_err());
    let archive = client().open(&output, "ipc-rar-secret", &op).unwrap();
    assert!(archive.writable);
    client().test(&archive, "ipc-rar-secret", &op).unwrap();
    client()
        .modify(
            &archive,
            &vynx_arc_core::modification::Change::Rename {
                old: "hello.txt".into(),
                new: "renamed.txt".into(),
            },
            "ipc-rar-secret",
            &op,
        )
        .unwrap();
    let archive = client().open(&output, "ipc-rar-secret", &op).unwrap();
    assert_eq!(archive.entries[0].name, "renamed.txt");
    assert!(client().open(&output, "", &op).is_err());
}

#[test]
fn cli_source_password_does_not_select_destination_encryption() {
    let root = tempfile::tempdir().unwrap();
    let input = root.path().join("input.txt");
    std::fs::write(&input, b"conversion credentials").unwrap();
    let source = root.path().join("source.zip");
    operations::create(
        &CreateOptions {
            output: source.clone(),
            inputs: vec![input],
            password: "source-only-secret".to_owned().into(),
        },
        &Operation::default(),
    )
    .unwrap();
    let before = std::fs::read(&source).unwrap();
    for allow in [false, true] {
        let output = root
            .path()
            .join(if allow { "allowed.zip" } else { "refused.zip" });
        let mut command = Command::new(env!("CARGO_BIN_EXE_vynxarc-cli"));
        command
            .arg("convert")
            .arg(&source)
            .arg(&output)
            .arg("--password-stdin");
        if allow {
            command.arg("--allow-decrypt");
        }
        let mut child = command
            .stdin(Stdio::piped())
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn()
            .unwrap();
        child
            .stdin
            .take()
            .unwrap()
            .write_all(b"source-only-secret\n")
            .unwrap();
        assert_eq!(child.wait().unwrap().success(), allow);
        assert_eq!(output.exists(), allow);
        if allow {
            let archive = vynx_arc_core::Archive::open(&output, "", &Operation::default()).unwrap();
            assert!(!archive.entries.iter().any(|e| e.encrypted));
            operations::test(&archive, "", &Operation::default()).unwrap();
        }
    }
    assert_eq!(std::fs::read(source).unwrap(), before);
}
#[test]
fn isolated_open_preview_test_hash_and_stale_metadata() {
    let dir = tempfile::tempdir().unwrap();
    let input = dir.path().join("hello.txt");
    std::fs::write(&input, b"worker verified text").unwrap();
    for ext in ["zip", "7z", "tar", "tar.gz", "xz"] {
        let path = dir.path().join(format!("source.{ext}"));
        operations::create(
            &CreateOptions {
                output: path.clone(),
                inputs: vec![input.clone()],
                password: zeroize::Zeroizing::new(String::new()),
            },
            &Operation::default(),
        )
        .unwrap();
        let client = client();
        let op = Operation::default();
        let a = client.open(&path, "", &op).unwrap();
        assert_eq!(client.text(&a, 0, "", &op).unwrap(), "worker verified text");
        assert_eq!(
            client.bytes(&a, 0, "", &op).unwrap(),
            b"worker verified text"
        );
        client.test(&a, "", &op).unwrap();
        assert_eq!(client.hashes(&a, &[], "", &op).unwrap().len(), 1);
        assert_eq!(
            client.hash_file(&path, &op).unwrap(),
            operations::hash_file(&path, &op).unwrap()
        );
        let mut stale = a.clone();
        stale.entries[0].size += 1;
        assert_eq!(
            client.bytes(&stale, 0, "", &op).unwrap_err().code,
            "CHANGED"
        );
    }
}
#[test]
fn encrypted_worker_and_cancelled_or_missing_worker() {
    let dir = tempfile::tempdir().unwrap();
    let input = dir.path().join("note.txt");
    std::fs::write(&input, b"secret data").unwrap();
    for ext in ["zip", "7z"] {
        let path = dir.path().join(format!("encrypted.{ext}"));
        operations::create(
            &CreateOptions {
                output: path.clone(),
                inputs: vec![input.clone()],
                password: zeroize::Zeroizing::new("ipc-only-password".into()),
            },
            &Operation::default(),
        )
        .unwrap();
        let op = Operation::default();
        let a = client().open(&path, "ipc-only-password", &op).unwrap();
        assert_eq!(
            client().bytes(&a, 0, "ipc-only-password", &op).unwrap(),
            b"secret data"
        );
        assert!(client().bytes(&a, 0, "wrong", &op).is_err());
        op.cancel();
        assert_eq!(
            client().open(&path, "", &op).err().unwrap().code,
            "CANCELLED"
        );
    }
    assert_eq!(
        Client::at(dir.path().join("missing.exe"), Duration::from_secs(1))
            .open(&input, "", &Operation::default())
            .err()
            .unwrap()
            .code,
        "WORKER_START"
    );
}
#[test]
fn timeout_kills_child_and_next_operation_still_works() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("not-archive");
    std::fs::write(&path, b"bad").unwrap();
    let c = Client::at(
        PathBuf::from(env!("CARGO_BIN_EXE_vynxarc-worker")),
        Duration::ZERO,
    );
    assert_eq!(
        c.open(&path, "", &Operation::default()).err().unwrap().code,
        "WORKER_TIMEOUT"
    );
    assert!(client().open(&path, "", &Operation::default()).is_err());
}
#[test]
fn malformed_protocol_exits_without_writing_requested_files() {
    for bytes in [
        b"VYNXWP99\0\0\0\0".as_slice(),
        b"VYNXWP03\0\0\0\0".as_slice(),
        b"VYNXWP04\xff\xff\xff\xff".as_slice(),
        b"VYNXWP04\x01\0\0\0\xff".as_slice(),
    ] {
        let mut child = Command::new(env!("CARGO_BIN_EXE_vynxarc-worker"))
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .spawn()
            .unwrap();
        child.stdin.take().unwrap().write_all(bytes).unwrap();
        assert!(!child.wait().unwrap().success());
    }
}

#[test]
fn publication_acknowledgement_precedes_creation_and_replacement() {
    let dir = tempfile::tempdir().unwrap();
    let input = dir.path().join("entry.txt");
    std::fs::write(&input, b"controlled write").unwrap();
    let output = dir.path().join("result.zip");
    let options = CreateOptions {
        output: output.clone(),
        inputs: vec![input],
        password: String::new().into(),
    };
    let op = Operation::default();
    op.control_publication(true);
    let copy = op.clone();
    let child =
        std::thread::spawn(move || client().create(&options, vynx_arc_core::Format::Zip, 0, &copy));
    let start = std::time::Instant::now();
    let (id, targets) = loop {
        if let Some(v) = op.pending_publication() {
            break v;
        }
        assert!(start.elapsed() < Duration::from_secs(10));
        std::thread::sleep(Duration::from_millis(10));
    };
    assert!(!output.exists());
    assert!(targets[0].before.is_empty());
    assert_eq!(targets[0].sha256.len(), 64);
    assert!(!op.reply_publication(id + 1, true));
    assert!(op.reply_publication(id, true));
    child.join().unwrap().unwrap();
    assert_eq!(
        vynx_arc_core::publication::sha256(&output, &Operation::default()).unwrap(),
        targets[0].sha256
    );
    let a = client().open(&output, "", &Operation::default()).unwrap();
    let before = std::fs::read(&output).unwrap();
    let old_hash = vynx_arc_core::publication::sha256(&output, &Operation::default()).unwrap();
    let op = Operation::default();
    op.control_publication(true);
    let copy = op.clone();
    let child = std::thread::spawn(move || {
        client().modify(
            &a,
            &vynx_arc_core::modification::Change::Rename {
                old: "entry.txt".into(),
                new: "renamed.txt".into(),
            },
            "",
            &copy,
        )
    });
    let start = std::time::Instant::now();
    let (id, targets) = loop {
        if let Some(v) = op.pending_publication() {
            break v;
        }
        assert!(start.elapsed() < Duration::from_secs(10));
        std::thread::sleep(Duration::from_millis(10));
    };
    assert_eq!(targets[0].before, old_hash);
    assert_eq!(std::fs::read(&output).unwrap(), before);
    assert!(op.reply_publication(id, false));
    assert_eq!(child.join().unwrap().unwrap_err().code, "JOURNAL");
    assert_eq!(std::fs::read(&output).unwrap(), before);
    let a = client().open(&output, "", &Operation::default()).unwrap();
    client()
        .modify(
            &a,
            &vynx_arc_core::modification::Change::Rename {
                old: "entry.txt".into(),
                new: "renamed.txt".into(),
            },
            "",
            &Operation::default(),
        )
        .unwrap();
    assert_eq!(
        client()
            .open(&output, "", &Operation::default())
            .unwrap()
            .entries[0]
            .name,
        "renamed.txt"
    );
}
#[test]
fn extraction_conflict_reply_crosses_process_boundary() {
    let dir = tempfile::tempdir().unwrap();
    let input = dir.path().join("note.txt");
    std::fs::write(&input, b"new bytes").unwrap();
    let archive = dir.path().join("source.7z");
    let options = CreateOptions {
        output: archive.clone(),
        inputs: vec![input],
        password: String::new().into(),
    };
    client()
        .create(
            &options,
            vynx_arc_core::Format::SevenZ,
            0,
            &Operation::default(),
        )
        .unwrap();
    let a = client().open(&archive, "", &Operation::default()).unwrap();
    let dest = dir.path().join("out");
    std::fs::create_dir(&dest).unwrap();
    std::fs::write(dest.join("note.txt"), b"old bytes").unwrap();
    let op = Operation::default();
    let copy = op.clone();
    let path = dest.clone();
    let child = std::thread::spawn(move || {
        client().extract(
            &a,
            &vynx_arc_core::ExtractOptions {
                destination: path,
                selected: vec![],
                conflict: vynx_arc_core::Conflict::Ask,
                smart: false,
                policy: Default::default(),
            },
            "",
            &copy,
        )
    });
    let start = std::time::Instant::now();
    let (id, details) = loop {
        if let Some(v) = op.pending_conflict() {
            break v;
        }
        assert!(start.elapsed() < Duration::from_secs(10));
        std::thread::sleep(Duration::from_millis(10));
    };
    assert_eq!(details.incoming_name, "note.txt");
    assert!(op.reply_conflict(id, vynx_arc_core::Conflict::Skip, false));
    child.join().unwrap().unwrap();
    assert_eq!(std::fs::read(dest.join("note.txt")).unwrap(), b"old bytes");
}
#[test]
fn interrupted_writer_never_claims_original_was_preserved() {
    let dir = tempfile::tempdir().unwrap();
    let input = dir.path().join("note.txt");
    std::fs::write(&input, b"bytes").unwrap();
    let options = CreateOptions {
        output: dir.path().join("result.zip"),
        inputs: vec![input],
        password: String::new().into(),
    };
    let c = Client::at(
        PathBuf::from(env!("CARGO_BIN_EXE_vynxarc-worker")),
        Duration::ZERO,
    );
    assert_eq!(
        c.create(
            &options,
            vynx_arc_core::Format::Zip,
            0,
            &Operation::default()
        )
        .unwrap_err()
        .code,
        "RESULT_UNKNOWN"
    );
}

#[test]
fn conversion_uses_worker_and_keeps_original_bytes() {
    let dir = tempfile::tempdir().unwrap();
    let input = dir.path().join("note.txt");
    std::fs::write(&input, b"worker conversion").unwrap();
    let path = dir.path().join("source.tar");
    operations::create(
        &CreateOptions {
            output: path.clone(),
            inputs: vec![input],
            password: String::new().into(),
        },
        &Operation::default(),
    )
    .unwrap();
    let before = std::fs::read(&path).unwrap();
    let op = Operation::default();
    let a = client().open(&path, "", &op).unwrap();
    let output = dir.path().join("converted.7z");
    client()
        .convert(
            &a,
            &vynx_arc_core::conversion::Options {
                output: output.clone(),
                format: vynx_arc_core::Format::SevenZ,
                password: zeroize::Zeroizing::new("destination-password".into()),
                allow_decrypt: false,
                replacement: None,
            },
            "",
            &op,
        )
        .unwrap();
    let result = client().open(&output, "destination-password", &op).unwrap();
    assert_eq!(
        client()
            .bytes(&result, 0, "destination-password", &op)
            .unwrap(),
        b"worker conversion"
    );
    assert_eq!(std::fs::read(&path).unwrap(), before);
}

#[test]
fn test_cli_multivolume_7z_modification() {
    let dir = tempfile::tempdir().unwrap();
    let file1 = dir.path().join("file1.bin");
    let file2 = dir.path().join("file2.bin");

    // Pseudo-random data to prevent excessive compression and force multi-volume split
    let mut data1 = vec![0u8; 70_000];
    let mut state = 123456789u32;
    for b in data1.iter_mut() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        *b = (state & 0xFF) as u8;
    }
    let mut data2 = vec![0u8; 70_000];
    for b in data2.iter_mut() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        *b = (state & 0xFF) as u8;
    }

    std::fs::write(&file1, &data1).unwrap();
    std::fs::write(&file2, &data2).unwrap();

    let output = dir.path().join("archive.7z");
    let op = Operation::default();
    operations::create_split(
        &CreateOptions {
            output: output.clone(),
            inputs: vec![file1, file2],
            password: String::new().into(),
        },
        65536,
        &op,
    )
    .unwrap();

    let first_part = dir.path().join("archive.7z.001");
    let second_part = dir.path().join("archive.7z.002");
    assert!(first_part.is_file(), "First part .001 must exist");
    assert!(second_part.is_file(), "Second part .002 must exist");

    let cli_exe = env!("CARGO_BIN_EXE_vynxarc-cli");

    // 1. CLI add
    let extra_file = dir.path().join("extra.txt");
    std::fs::write(&extra_file, b"extra content to add").unwrap();
    let status_add = Command::new(cli_exe)
        .arg("add")
        .arg(&first_part)
        .arg(&extra_file)
        .status()
        .unwrap();
    assert!(status_add.success(), "CLI add should succeed");

    let archive = vynx_arc_core::Archive::open(&first_part, "", &Operation::default()).unwrap();
    assert!(
        archive.entries.iter().any(|e| e.name == "extra.txt"),
        "extra.txt must be added"
    );
    assert!(
        second_part.is_file(),
        "Multi-volume parts must be maintained"
    );

    // 2. CLI rename
    let status_rename = Command::new(cli_exe)
        .arg("rename")
        .arg(&first_part)
        .arg("extra.txt")
        .arg("renamed.txt")
        .status()
        .unwrap();
    assert!(status_rename.success(), "CLI rename should succeed");

    let archive = vynx_arc_core::Archive::open(&first_part, "", &Operation::default()).unwrap();
    assert!(
        archive.entries.iter().any(|e| e.name == "renamed.txt"),
        "renamed.txt must exist"
    );
    assert!(
        !archive.entries.iter().any(|e| e.name == "extra.txt"),
        "extra.txt must be removed"
    );

    // 3. CLI delete
    let status_delete = Command::new(cli_exe)
        .arg("delete")
        .arg(&first_part)
        .arg("renamed.txt")
        .status()
        .unwrap();
    assert!(status_delete.success(), "CLI delete should succeed");

    let archive = vynx_arc_core::Archive::open(&first_part, "", &Operation::default()).unwrap();
    assert!(
        !archive.entries.iter().any(|e| e.name == "renamed.txt"),
        "renamed.txt must be deleted"
    );

    // Verify final archive integrity
    operations::test(&archive, "", &Operation::default()).unwrap();
    assert!(vynx_arc_core::split_rebuild::detect_volume_pattern(&first_part).is_some());
}
