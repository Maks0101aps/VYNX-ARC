#![cfg(windows)]
use std::{fs, process::Command};
use vynx_arc_core::security;
#[test]
fn rejects_junction_destination() {
    let t = tempfile::tempdir().unwrap();
    let actual = t.path().join("actual");
    let link = t.path().join("link");
    fs::create_dir(&actual).unwrap();
    let status = Command::new("cmd")
        .args(["/c", "mklink", "/J"])
        .arg(&link)
        .arg(&actual)
        .output()
        .unwrap();
    assert!(status.status.success(), "junction fixture creation failed");
    assert!(security::output_path(t.path(), "link/escape.txt").is_err());
    assert!(security::pin_ancestors(&link).is_err());
    fs::remove_dir(&link).unwrap();
}
#[test]
fn pinned_directory_cannot_be_renamed() {
    let t = tempfile::tempdir().unwrap();
    let root = t.path().join("root");
    fs::create_dir(&root).unwrap();
    let handles = security::pin_ancestors(&root).unwrap();
    assert!(fs::rename(&root, t.path().join("moved")).is_err());
    drop(handles);
    fs::rename(&root, t.path().join("moved")).unwrap();
}
#[test]
fn motw_propagates_and_oversized_zone_is_not_published() {
    use vynx_arc_core::{operations, *};
    let t = tempfile::tempdir().unwrap();
    let input = t.path().join("file.txt");
    fs::write(&input, "untrusted content").unwrap();
    let output = t.path().join("archive.zip");
    operations::create(
        &CreateOptions {
            output: output.clone(),
            inputs: vec![input],
            password: String::new().into(),
        },
        &Operation::default(),
    )
    .unwrap();
    let zone = format!("{}:Zone.Identifier", output.display());
    let expected = b"[ZoneTransfer]\r\nZoneId=3\r\n";
    fs::write(&zone, expected).unwrap();
    let archive = Archive::open(&output, "", &Operation::default()).unwrap();
    let options = |name| ExtractOptions {
        destination: t.path().join(name),
        selected: vec![],
        conflict: Conflict::Refuse,
        smart: false,
        policy: security::Policy::default(),
    };
    operations::extract(&archive, &options("out"), "", &Operation::default()).unwrap();
    assert_eq!(
        fs::read(format!(
            "{}:Zone.Identifier",
            t.path().join("out/file.txt").display()
        ))
        .unwrap(),
        expected
    );
    fs::write(&zone, vec![b'x'; 64 * 1024 + 1]).unwrap();
    let error =
        operations::extract(&archive, &options("blocked"), "", &Operation::default()).unwrap_err();
    assert_eq!(error.code, "MOTW");
    assert!(!t.path().join("blocked/file.txt").exists());
    assert_eq!(fs::read_dir(t.path().join("blocked")).unwrap().count(), 0);
}
