#![cfg(windows)]
use std::{
    fs,
    path::{Path, PathBuf},
};
use vynx_arc_core::{Archive, Conflict, ExtractOptions, Operation, operations, security::Policy};
fn fixture(name: &str) -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../tests/archives")
        .join(name)
}
#[test]
fn rar5_stored_and_compressed_real_fixtures() {
    for name in [
        "test_read_format_rar5_stored.rar",
        "test_read_format_rar5_compressed.rar",
        "test_read_format_rar5_multiple_files.rar",
        "test_read_format_rar_binary_data.rar",
    ] {
        let archive = Archive::open(&fixture(name), "", &Operation::default()).unwrap();
        operations::test(&archive, "", &Operation::default()).unwrap();
        let t = tempfile::tempdir().unwrap();
        operations::extract(
            &archive,
            &ExtractOptions {
                destination: t.path().into(),
                selected: vec![],
                conflict: Conflict::Refuse,
                smart: false,
                policy: Policy::default(),
            },
            "",
            &Operation::default(),
        )
        .unwrap();
        for e in archive.entries.iter().filter(|e| !e.directory) {
            assert_eq!(fs::metadata(t.path().join(&e.name)).unwrap().len(), e.size);
        }
        if name.contains("stored.rar") {
            assert_eq!(
                fs::read(t.path().join("helloworld.txt")).unwrap(),
                b"hello libarchive test suite!\n"
            );
        }
    }
}
#[test]
fn rar_links_are_detected_and_blocked() {
    for name in [
        "test_read_format_rar.rar",
        "test_read_format_rar5_unicode.rar",
        "test_read_format_rar_compress_normal.rar",
    ] {
        let archive = Archive::open(&fixture(name), "", &Operation::default()).unwrap();
        assert!(archive.entries.iter().any(|e| e.link));
        let t = tempfile::tempdir().unwrap();
        let dest = t.path().join("out");
        assert!(
            operations::extract(
                &archive,
                &ExtractOptions {
                    destination: dest.clone(),
                    selected: vec![],
                    conflict: Conflict::Refuse,
                    smart: false,
                    policy: Policy::default()
                },
                "",
                &Operation::default()
            )
            .is_err()
        );
        assert!(!dest.exists());
    }
}

#[test]
fn rar4_and_rar5_encrypted_headers_and_data() {
    for name in [
        "test_read_format_rar4_encrypted_filenames.rar",
        "test_read_format_rar5_encrypted_filenames.rar",
    ] {
        let path = fixture(name);
        assert!(Archive::open(&path, "wrong", &Operation::default()).is_err());
        let archive = Archive::open(&path, "password", &Operation::default()).unwrap();
        operations::test(&archive, "password", &Operation::default()).unwrap();
        let t = tempfile::tempdir().unwrap();
        operations::extract(
            &archive,
            &ExtractOptions {
                destination: t.path().into(),
                selected: vec![],
                conflict: Conflict::Refuse,
                smart: false,
                policy: Policy::default(),
            },
            "password",
            &Operation::default(),
        )
        .unwrap();
        assert_eq!(
            fs::read(t.path().join("a.txt")).unwrap(),
            b"This is from a.txt"
        );
    }
}

#[test]
fn concurrent_rar_fixtures_repeat_open_list_and_test_without_shared_state_leaks() {
    let cases = [
        ("test_read_format_rar_binary_data.rar", ""),
        ("test_read_format_rar5_stored.rar", ""),
        ("test_read_format_rar4_encrypted_filenames.rar", "password"),
        ("test_read_format_rar5_encrypted_filenames.rar", "password"),
        ("test_rar_multivolume_single_file.part1.rar", ""),
        ("test_read_format_rar5_multiarchive_solid.part01.rar", ""),
    ];
    let workers: Vec<_> = cases
        .into_iter()
        .map(|(name, password)| {
            let path = fixture(name);
            std::thread::spawn(move || {
                for iteration in 0..20 {
                    let archive = Archive::open(&path, password, &Operation::default())
                        .unwrap_or_else(|e| panic!("{name} open {iteration}: {e}"));
                    operations::test(&archive, password, &Operation::default())
                        .unwrap_or_else(|e| panic!("{name} test {iteration}: {e}"));
                }
            })
        })
        .collect();
    for worker in workers {
        worker.join().expect("RAR worker panicked");
    }
}
