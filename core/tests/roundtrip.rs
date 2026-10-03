use std::{fs, path::Path};
use vynx_arc_core::{operations, security::Policy, *};

fn extraction(dest: &Path) -> ExtractOptions {
    ExtractOptions {
        destination: dest.into(),
        selected: vec![],
        conflict: Conflict::Refuse,
        smart: false,
        policy: Policy::default(),
    }
}

#[test]
fn real_roundtrips_all_writable_formats() {
    for extension in ["zip", "7z", "tar", "tar.gz"] {
        let t = tempfile::tempdir().unwrap();
        let input = t.path().join("проект");
        fs::create_dir_all(input.join("empty-folder")).unwrap();
        fs::write(input.join("hello.txt"), b"hello archive\n").unwrap();
        fs::write(input.join("порожній.txt"), b"").unwrap();
        let binary: Vec<u8> = (0..200_000).map(|i| (i % 251) as u8).collect();
        fs::write(input.join("data.bin"), &binary).unwrap();
        let output = t.path().join(format!("sample.{extension}"));
        operations::create(
            &CreateOptions {
                output: output.clone(),
                inputs: vec![input],
                password: zeroize::Zeroizing::new(String::new()),
            },
            &Operation::default(),
        )
        .unwrap();
        let archive = Archive::open(&output, "", &Operation::default()).unwrap();
        if extension == "7z" {
            let decoder =
                sevenz_rust2::ArchiveReader::new(fs::File::open(&output).unwrap(), "".into())
                    .unwrap();
            for e in &decoder.archive().files {
                assert!(e.has_windows_attributes);
                assert_eq!(e.windows_attributes & 0x10 != 0, e.is_directory);
            }
        }
        operations::test(&archive, "", &Operation::default()).unwrap();
        let dest = t.path().join("out");
        operations::extract(&archive, &extraction(&dest), "", &Operation::default()).unwrap();
        assert_eq!(fs::read(dest.join("проект/data.bin")).unwrap(), binary);
        assert_eq!(
            fs::read(dest.join("проект/hello.txt")).unwrap(),
            b"hello archive\n"
        );
        assert!(dest.join("проект/empty-folder").is_dir());
        assert_eq!(
            fs::metadata(dest.join("проект/порожній.txt"))
                .unwrap()
                .len(),
            0
        );
    }
}

#[test]
fn encrypted_zip_and_7z_correct_and_wrong_passwords() {
    for ext in ["zip", "7z"] {
        let t = tempfile::tempdir().unwrap();
        let input = t.path().join("secret.txt");
        fs::write(&input, b"private data").unwrap();
        let output = t.path().join(format!("secret.{ext}"));
        operations::create(
            &CreateOptions {
                output: output.clone(),
                inputs: vec![input],
                password: zeroize::Zeroizing::new("test-password".into()),
            },
            &Operation::default(),
        )
        .unwrap();
        let archive = Archive::open(&output, "test-password", &Operation::default()).unwrap();
        operations::extract(
            &archive,
            &extraction(&t.path().join("out")),
            "test-password",
            &Operation::default(),
        )
        .unwrap();
        assert_eq!(
            fs::read(t.path().join("out/secret.txt")).unwrap(),
            b"private data"
        );
        let wrong = Archive::open(&output, "wrong", &Operation::default());
        assert!(
            wrong.is_err()
                || operations::test(&wrong.unwrap(), "wrong", &Operation::default()).is_err()
        );
    }
}

#[test]
fn refuses_existing_output_preserves_user_data() {
    let t = tempfile::tempdir().unwrap();
    let source = t.path().join("file.txt");
    fs::write(&source, b"new").unwrap();
    let output = t.path().join("sample.zip");
    operations::create(
        &CreateOptions {
            output: output.clone(),
            inputs: vec![source.clone()],
            password: zeroize::Zeroizing::new(String::new()),
        },
        &Operation::default(),
    )
    .unwrap();
    let original = fs::read(&output).unwrap();
    assert!(
        operations::create(
            &CreateOptions {
                output: output.clone(),
                inputs: vec![source],
                password: zeroize::Zeroizing::new(String::new())
            },
            &Operation::default()
        )
        .is_err()
    );
    assert_eq!(fs::read(&output).unwrap(), original);
    let dest = t.path().join("out");
    fs::create_dir(&dest).unwrap();
    fs::write(dest.join("file.txt"), b"keep").unwrap();
    let archive = Archive::open(&output, "", &Operation::default()).unwrap();
    assert!(operations::extract(&archive, &extraction(&dest), "", &Operation::default()).is_err());
    assert_eq!(fs::read(dest.join("file.txt")).unwrap(), b"keep");
}

#[test]
fn malicious_archive_is_rejected_before_any_file_output() {
    use std::io::Write;
    let t = tempfile::tempdir().unwrap();
    let output = t.path().join("bad.zip");
    let mut zip = zip::ZipWriter::new(fs::File::create(&output).unwrap());
    zip.start_file("good.txt", zip::write::SimpleFileOptions::default())
        .unwrap();
    zip.write_all(b"good").unwrap();
    zip.start_file("../escape.txt", zip::write::SimpleFileOptions::default())
        .unwrap();
    zip.write_all(b"evil").unwrap();
    zip.finish().unwrap();
    let archive = Archive::open(&output, "", &Operation::default()).unwrap();
    let dest = t.path().join("out");
    assert!(operations::extract(&archive, &extraction(&dest), "", &Operation::default()).is_err());
    assert!(!dest.exists());
    assert!(!t.path().join("escape.txt").exists());
}

#[test]
fn cancellation_has_no_published_output() {
    let t = tempfile::tempdir().unwrap();
    let input = t.path().join("source");
    fs::write(&input, b"data").unwrap();
    let output = t.path().join("cancel.zip");
    let op = Operation::default();
    op.cancel();
    let err = operations::create(
        &CreateOptions {
            output: output.clone(),
            inputs: vec![input],
            password: zeroize::Zeroizing::new(String::new()),
        },
        &op,
    )
    .unwrap_err();
    assert_eq!(err.code, "CANCELLED");
    assert!(!output.exists());
}

#[test]
fn corrupted_archives_fail_safely() {
    let t = tempfile::tempdir().unwrap();
    for bytes in [
        vec![],
        b"random bytes".to_vec(),
        b"PK\x03\x04broken".to_vec(),
        b"7z\xbc\xaf\x27\x1cbroken".to_vec(),
    ] {
        let p = t.path().join("bad.zip");
        fs::write(&p, bytes).unwrap();
        assert!(Archive::open(&p, "", &Operation::default()).is_err());
    }
}

#[test]
fn corrupt_payload_is_not_published_and_temp_is_cleaned() {
    use std::io::Write;
    let t = tempfile::tempdir().unwrap();
    let path = t.path().join("bad.zip");
    let mut writer = zip::ZipWriter::new(fs::File::create(&path).unwrap());
    writer
        .start_file(
            "data.txt",
            zip::write::SimpleFileOptions::default()
                .compression_method(zip::CompressionMethod::Stored),
        )
        .unwrap();
    writer.write_all(b"original-data").unwrap();
    writer.finish().unwrap();
    let mut bytes = fs::read(&path).unwrap();
    let pos = bytes
        .windows(13)
        .position(|x| x == b"original-data")
        .unwrap();
    bytes[pos] ^= 1;
    fs::write(&path, bytes).unwrap();
    let archive = Archive::open(&path, "", &Operation::default()).unwrap();
    let dest = t.path().join("out");
    assert!(operations::extract(&archive, &extraction(&dest), "", &Operation::default()).is_err());
    assert!(!dest.join("data.txt").exists());
    assert_eq!(fs::read_dir(&dest).unwrap().count(), 0);
}

#[test]
fn cancellation_mid_stream_keeps_existing_files_and_removes_partial() {
    use std::io::Write;
    let t = tempfile::tempdir().unwrap();
    let path = t.path().join("large.zip");
    let mut writer = zip::ZipWriter::new(fs::File::create(&path).unwrap());
    writer
        .start_file(
            "large.bin",
            zip::write::SimpleFileOptions::default()
                .compression_method(zip::CompressionMethod::Stored),
        )
        .unwrap();
    let block = [42u8; 128 * 1024];
    for _ in 0..512 {
        writer.write_all(&block).unwrap();
    }
    writer.finish().unwrap();
    let archive = Archive::open(&path, "", &Operation::default()).unwrap();
    let dest = t.path().join("out");
    fs::create_dir(&dest).unwrap();
    fs::write(dest.join("keep.txt"), b"keep").unwrap();
    let op = Operation::default();
    let cancel = op.clone();
    let thread = std::thread::spawn(move || {
        while cancel.snapshot().done == 0 {
            std::thread::yield_now();
        }
        cancel.cancel();
    });
    assert_eq!(
        operations::extract(&archive, &extraction(&dest), "", &op)
            .unwrap_err()
            .code,
        "CANCELLED"
    );
    thread.join().unwrap();
    assert!(!dest.join("large.bin").exists());
    assert_eq!(fs::read(dest.join("keep.txt")).unwrap(), b"keep");
    assert_eq!(fs::read_dir(&dest).unwrap().count(), 1);
}

#[test]
fn hashes_match_known_vectors() {
    let t = tempfile::tempdir().unwrap();
    let p = t.path().join("data");
    fs::write(&p, b"123456789").unwrap();
    let hash = operations::hash_file(&p, &Operation::default()).unwrap();
    assert_eq!(
        hash,
        "SHA-256: 15e2b0d3c33891ebb0f1ef609ec419420c20e320ce94c65fbc8c3312448eb225\nCRC32: cbf43926"
    );
}

#[test]
fn changed_zip_count_or_crc_since_open_is_rejected_without_publication() {
    use std::io::Write;
    let t = tempfile::tempdir().unwrap();
    let path = t.path().join("changed.zip");
    let write_zip = |content: &[u8], extra: bool| {
        let mut writer = zip::ZipWriter::new(fs::File::create(&path).unwrap());
        let options = zip::write::SimpleFileOptions::default()
            .compression_method(zip::CompressionMethod::Stored);
        writer.start_file("file.txt", options).unwrap();
        writer.write_all(content).unwrap();
        if extra {
            writer.start_file("extra.txt", options).unwrap();
            writer.write_all(b"extra").unwrap();
        }
        writer.finish().unwrap();
    };
    for extra in [false, true] {
        write_zip(b"before", false);
        let archive = Archive::open(&path, "", &Operation::default()).unwrap();
        // Same name and size with different CRC, or a different entry count.
        write_zip(b"after!", extra);
        let dest = t.path().join(if extra { "count" } else { "crc" });
        let error = operations::extract(&archive, &extraction(&dest), "", &Operation::default())
            .unwrap_err();
        assert_eq!(error.code, "CHANGED");
        assert!(!dest.join("file.txt").exists());
        assert_eq!(fs::read_dir(&dest).unwrap().count(), 0);
    }
}
