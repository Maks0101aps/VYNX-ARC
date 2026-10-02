use std::{fs, path::Path};
use vynx_arc_core::{hashing, operations, *};
const SHA: &str = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
#[test]
fn decoded_entry_hashes_and_verification_across_formats() {
    for ext in ["zip", "7z", "tar", "tar.gz"] {
        for password in if matches!(ext, "zip" | "7z") {
            vec!["", "secret"]
        } else {
            vec![""]
        } {
            let t = tempfile::tempdir().unwrap();
            let source = t.path().join("дані.txt");
            fs::write(&source, b"abc").unwrap();
            let empty = t.path().join("empty.txt");
            fs::write(&empty, b"").unwrap();
            let path = t.path().join(format!("a.{ext}"));
            operations::create(
                &CreateOptions {
                    output: path.clone(),
                    inputs: vec![source, empty],
                    password: password.to_string().into(),
                },
                &Operation::default(),
            )
            .unwrap();
            let archive = Archive::open(&path, password, &Operation::default()).unwrap();
            let id = archive
                .entries
                .iter()
                .find(|e| e.name == "дані.txt")
                .unwrap()
                .id;
            let h = hashing::entries(&archive, &[id], password, &Operation::default()).unwrap();
            assert_eq!(h.len(), 1);
            assert_eq!(h[0].sha256, SHA);
            assert_eq!(h[0].crc32, "352441c2");
            hashing::verify(&h[0].sha256, &h[0].crc32, &SHA.to_uppercase()).unwrap();
            hashing::verify(&h[0].sha256, &h[0].crc32, "352441c2").unwrap();
            assert_eq!(
                hashing::verify(&h[0].sha256, &h[0].crc32, "00000000")
                    .unwrap_err()
                    .code,
                "HASH_MISMATCH"
            );
            assert_eq!(
                hashing::entries(&archive, &[u64::MAX], password, &Operation::default())
                    .unwrap_err()
                    .code,
                "SELECTION"
            );
            let all = hashing::entries(&archive, &[], password, &Operation::default()).unwrap();
            assert_eq!(all.len(), 2);
            assert_eq!(
                all.iter().find(|h| h.name == "empty.txt").unwrap().sha256,
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
            );
            let op = Operation::default();
            op.cancel();
            assert_eq!(
                hashing::entries(&archive, &[id], password, &op)
                    .unwrap_err()
                    .code,
                "CANCELLED"
            );
        }
    }
}
#[test]
fn file_verification_and_invalid_input() {
    let t = tempfile::tempdir().unwrap();
    let p = t.path().join("file");
    fs::write(&p, b"abc").unwrap();
    hashing::verify_file(&p, SHA, &Operation::default()).unwrap();
    hashing::verify_file(&p, "352441C2", &Operation::default()).unwrap();
    let report = hashing::verify_file_report(&p, SHA, &Operation::default()).unwrap();
    assert!(report.starts_with("MATCH\nAlgorithm: SHA-256\n"));
    assert!(report.contains(&format!("Actual: {SHA}")));
    let error = hashing::verify_file_report(&p, "00000000", &Operation::default()).unwrap_err();
    assert_eq!(error.code, "HASH_MISMATCH");
    assert!(error.message.contains("DOES NOT MATCH\nAlgorithm: CRC32"));
    assert!(
        error
            .message
            .contains("Actual: 352441c2\nExpected: 00000000")
    );
    for bad in [
        "",
        "xyz",
        "1234567z",
        "SHA-256: abc",
        "0".repeat(65).as_str(),
    ] {
        assert_eq!(
            hashing::verify_file(Path::new("not-existing"), bad, &Operation::default())
                .unwrap_err()
                .code,
            "HASH_FORMAT"
        );
    }
    assert_eq!(
        hashing::verify_file(&p, "00000000", &Operation::default())
            .unwrap_err()
            .code,
        "HASH_MISMATCH"
    );
}
