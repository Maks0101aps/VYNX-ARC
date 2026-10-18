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
        b"VYNXWP01\xff\xff\xff\xff".as_slice(),
        b"VYNXWP01\x01\0\0\0\xff".as_slice(),
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
