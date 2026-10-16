use std::fs;
use vynx_arc_core::{operations, preview, *};

#[test]
fn preview_is_verified_plain_text_and_never_publishes_plaintext() {
    for ext in ["zip", "7z", "tar", "tar.gz"] {
        let t = tempfile::tempdir().unwrap();
        let input = t.path().join("readme.txt");
        fs::write(&input, "<b>Текст</b>\nhello\tworld").unwrap();
        let path = t.path().join(format!("archive.{ext}"));
        operations::create(
            &CreateOptions {
                output: path.clone(),
                inputs: vec![input.clone()],
                password: String::new().into(),
            },
            &Operation::default(),
        )
        .unwrap();
        fs::remove_file(input).unwrap();
        let a = Archive::open(&path, "", &Operation::default()).unwrap();
        assert_eq!(
            preview::text(&a, a.entries[0].id, "", &Operation::default()).unwrap(),
            "<b>Текст</b>\nhello\tworld"
        );
        assert_eq!(fs::read_dir(t.path()).unwrap().count(), 1);
    }
}

#[test]
fn preview_rejects_binary_oversized_wrong_password_and_cancellation() {
    for data in [
        vec![0, 1, 255],
        vec![b'x'; (preview::MAX_PREVIEW_BYTES + 1) as usize],
    ] {
        let t = tempfile::tempdir().unwrap();
        let input = t.path().join("data.bin");
        fs::write(&input, data).unwrap();
        let path = t.path().join("encrypted.zip");
        operations::create(
            &CreateOptions {
                output: path.clone(),
                inputs: vec![input],
                password: String::from("secret").into(),
            },
            &Operation::default(),
        )
        .unwrap();
        let a = Archive::open(&path, "secret", &Operation::default()).unwrap();
        assert!(preview::text(&a, 0, "wrong", &Operation::default()).is_err());
        assert!(preview::text(&a, 0, "secret", &Operation::default()).is_err());
        let cancelled = Operation::default();
        cancelled.cancel();
        assert!(preview::text(&a, 0, "secret", &cancelled).is_err());
    }
}
