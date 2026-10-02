use std::{fs, path::Path};
use vynx_arc_core::{
    modification::{self, Change},
    operations,
    security::Policy,
    *,
};
fn noise() -> Vec<u8> {
    let mut state = 123456789u64;
    (0..256 * 1024 + 17)
        .map(|_| {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            state as u8
        })
        .collect()
}
#[test]
fn split_7z_roundtrip_missing_part_and_read_only() {
    for password in ["", "private"] {
        let t = tempfile::tempdir().unwrap();
        let input = t.path().join("дані.bin");
        let data = noise();
        fs::write(&input, &data).unwrap();
        let output = t.path().join("archive.7z");
        operations::create_split(
            &CreateOptions {
                output: output.clone(),
                inputs: vec![input],
                password: password.to_string().into(),
            },
            65536,
            &Operation::default(),
        )
        .unwrap();
        assert!(!output.exists());
        let first = t.path().join("archive.7z.001");
        let archive = Archive::open(
            &t.path().join("archive.7z.002"),
            password,
            &Operation::default(),
        )
        .unwrap();
        assert_eq!(archive.path, first);
        operations::test(&archive, password, &Operation::default()).unwrap();
        operations::extract(
            &archive,
            &ExtractOptions {
                destination: t.path().join("out"),
                selected: vec![],
                conflict: Conflict::Refuse,
                smart: false,
                policy: Policy::default(),
            },
            password,
            &Operation::default(),
        )
        .unwrap();
        assert_eq!(fs::read(t.path().join("out/дані.bin")).unwrap(), data);
        assert_eq!(
            modification::modify(
                &archive,
                &Change::Delete {
                    names: vec!["дані.bin".into()]
                },
                password,
                &Operation::default()
            )
            .unwrap_err()
            .code,
            "READ_ONLY"
        );
        fs::rename(
            t.path().join("archive.7z.002"),
            t.path().join("hidden-part"),
        )
        .unwrap();
        let error = match Archive::open(&first, password, &Operation::default()) {
            Err(e) => e,
            Ok(_) => panic!("Missing volume accepted"),
        };
        assert_eq!(error.code, "MISSING_VOLUME");
        assert!(error.message.contains("archive.7z.002"));
    }
}
#[test]
fn split_refuses_existing_parts_and_bad_configuration() {
    let t = tempfile::tempdir().unwrap();
    let source = t.path().join("file");
    fs::write(&source, noise()).unwrap();
    let options = CreateOptions {
        output: t.path().join("a.7z"),
        inputs: vec![source],
        password: String::new().into(),
    };
    fs::write(t.path().join("a.7z.002"), "existing user data").unwrap();
    assert_eq!(
        operations::create_split(&options, 65536, &Operation::default())
            .unwrap_err()
            .code,
        "CONFLICT"
    );
    assert_eq!(
        fs::read(t.path().join("a.7z.002")).unwrap(),
        b"existing user data"
    );
    assert!(!t.path().join("a.7z.001").exists());
    assert_eq!(
        operations::create_split(&options, 1, &Operation::default())
            .unwrap_err()
            .code,
        "OPTIONS"
    );
    let op = Operation::default();
    op.cancel();
    assert_eq!(
        operations::create_split(&options, 65536, &op)
            .unwrap_err()
            .code,
        "CANCELLED"
    );
}
#[cfg(windows)]
#[test]
fn rar4_rar5_multivolume_fixtures_and_exact_missing_path() {
    for (prefix, count, width) in [
        ("test_rar_multivolume_single_file", 3, 1),
        ("test_read_format_rar5_multiarchive_solid", 4, 2),
    ] {
        let t = tempfile::tempdir().unwrap();
        for i in 1..=count {
            let name = format!("{prefix}.part{i:0width$}.rar");
            let path = Path::new(env!("CARGO_MANIFEST_DIR"))
                .join("../tests/archives")
                .join(&name);
            fs::copy(path, t.path().join(name)).unwrap();
        }
        let first = t.path().join(format!("{prefix}.part{:0width$}.rar", 1));
        let archive = Archive::open(&first, "", &Operation::default()).unwrap();
        operations::test(&archive, "", &Operation::default()).unwrap();
        operations::extract(
            &archive,
            &ExtractOptions {
                destination: t.path().join("out"),
                selected: vec![],
                conflict: Conflict::Refuse,
                smart: false,
                policy: Policy::default(),
            },
            "",
            &Operation::default(),
        )
        .unwrap();
        fs::remove_file(t.path().join(format!("{prefix}.part{:0width$}.rar", 2))).unwrap();
        let error = match Archive::open(&first, "", &Operation::default()) {
            Err(e) => e,
            Ok(a) => operations::test(&a, "", &Operation::default()).unwrap_err(),
        };
        assert_eq!(error.code, "MISSING_VOLUME");
        assert!(
            error
                .message
                .contains(&format!("{prefix}.part{:0width$}.rar", 2))
        );
    }
}
#[cfg(windows)]
#[test]
fn multivolume_link_archive_remains_blocked() {
    let path = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../tests/archives/test_rar_multivolume_uncompressed_files.part01.rar");
    let archive = Archive::open(&path, "", &Operation::default()).unwrap();
    let t = tempfile::tempdir().unwrap();
    let error = operations::extract(
        &archive,
        &ExtractOptions {
            destination: t.path().join("out"),
            selected: vec![],
            conflict: Conflict::Refuse,
            smart: false,
            policy: Policy::default(),
        },
        "",
        &Operation::default(),
    )
    .unwrap_err();
    assert_eq!(error.code, "LINK");
    assert!(!t.path().join("out").exists());
}
#[cfg(windows)]
#[test]
fn cancelled_split_cleanup_does_not_delete_a_replacement_path() {
    use std::{
        io::Write,
        time::{Duration, Instant},
    };
    let t = tempfile::tempdir().unwrap();
    let source = t.path().join("source.dat");
    let mut file = fs::File::create(&source).unwrap();
    for _ in 0..512 {
        file.write_all(&[42u8; 65536]).unwrap();
    }
    drop(file);
    let mut source = fs::File::open(&source).unwrap();
    let prefix = t.path().join("new.7z");
    let first = t.path().join("new.7z.001");
    let replaced = first.clone();
    let moved = t.path().join("owned-part-moved");
    let moved_actor = moved.clone();
    let op = Operation::default();
    let actor = op.clone();
    let thread = std::thread::spawn(move || {
        let deadline = Instant::now() + Duration::from_secs(10);
        while !replaced.exists() {
            assert!(Instant::now() < deadline);
            std::thread::sleep(Duration::from_micros(100));
        }
        fs::rename(&replaced, &moved_actor).unwrap();
        fs::write(&replaced, "user replacement must survive").unwrap();
        actor.cancel();
    });
    let error = vynx_arc_core::volumes::publish(&mut source, &prefix, 65536, &op).unwrap_err();
    thread.join().unwrap();
    assert_eq!(error.code, "CANCELLED");
    assert_eq!(fs::read(&first).unwrap(), b"user replacement must survive");
    assert!(!moved.exists());
    assert_eq!(fs::read_dir(t.path()).unwrap().count(), 2);
}
