use std::{fs, io::Write};
use vynx_arc_core::{
    Archive, Operation,
    operations::{self, Conflict, ExtractOptions},
    security::Policy,
};
#[test]
fn unselected_destination_is_not_inspected_or_created() {
    let root = tempfile::tempdir().unwrap();
    let path = root.path().join("selected.zip");
    let mut zip = zip::ZipWriter::new(fs::File::create(&path).unwrap());
    for name in ["wanted.txt", "blocked/unused.txt"] {
        zip.start_file(name, zip::write::SimpleFileOptions::default())
            .unwrap();
        zip.write_all(b"verified bytes").unwrap();
    }
    zip.finish().unwrap();
    let archive = Archive::open(&path, "", &Operation::default()).unwrap();
    let destination = root.path().join("out");
    fs::create_dir(&destination).unwrap();
    fs::write(destination.join("blocked"), b"keep existing file").unwrap();
    operations::extract(
        &archive,
        &ExtractOptions {
            destination: destination.clone(),
            selected: vec![0],
            conflict: Conflict::Refuse,
            smart: false,
            policy: Policy::default(),
        },
        "",
        &Operation::default(),
    )
    .unwrap();
    assert_eq!(
        fs::read(destination.join("wanted.txt")).unwrap(),
        b"verified bytes"
    );
    assert_eq!(
        fs::read(destination.join("blocked")).unwrap(),
        b"keep existing file"
    );
}
