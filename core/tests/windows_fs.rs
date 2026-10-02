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
