use std::{
    fs,
    path::{Path, PathBuf},
};
use vynx_arc_core::{
    Archive, Conflict, CreateOptions, ExtractOptions, Operation,
    modification::{Change, modify},
    operations,
    security::Policy,
    split_rebuild::existing_volume_parts,
};

fn pseudo_random_bytes(len: usize, mut seed: u64) -> Vec<u8> {
    (0..len)
        .map(|_| {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            seed as u8
        })
        .collect()
}

fn create_split_7z(
    dir: &Path,
    name: &str,
    files: &[(&str, &[u8])],
    volume_size: u64,
) -> (PathBuf, Vec<PathBuf>) {
    let input_dir = dir.join("inputs");
    fs::create_dir_all(&input_dir).unwrap();

    let mut input_paths = Vec::new();
    for (file_name, data) in files {
        let file_path = input_dir.join(file_name);
        if let Some(parent) = file_path.parent() {
            fs::create_dir_all(parent).unwrap();
        }
        fs::write(&file_path, data).unwrap();
        input_paths.push(file_path);
    }

    let output = dir.join(format!("{name}.7z"));
    let options = CreateOptions {
        output,
        inputs: input_paths,
        password: String::new().into(),
    };

    let op = Operation::default();
    operations::create_split(&options, volume_size, &op).unwrap();

    let first_part = dir.join(format!("{name}.7z.001"));
    assert!(first_part.is_file(), "First part must exist");

    let parts = existing_volume_parts(&first_part).unwrap();
    (first_part, parts)
}

#[test]
fn test_multivolume_7z_add() {
    let t = tempfile::tempdir().unwrap();
    let data1 = pseudo_random_bytes(70_000, 111);
    let data2 = pseudo_random_bytes(70_000, 222);

    let (first, initial_parts) = create_split_7z(
        t.path(),
        "archive_add",
        &[("file1.bin", &data1), ("file2.bin", &data2)],
        65536,
    );
    assert!(
        initial_parts.len() >= 2,
        "Expected at least 2 parts initially"
    );

    let op = Operation::default();
    let archive = Archive::open(&first, "", &op).unwrap();
    assert!(
        archive.writable,
        "Multi-volume 7Z archive should be writable"
    );
    assert_eq!(archive.entries.len(), 2);

    // Prepare a 3rd file to add
    let data3 = pseudo_random_bytes(80_000, 333);
    let add_file = t.path().join("file3.bin");
    fs::write(&add_file, &data3).unwrap();

    // Perform Add change
    modify(
        &archive,
        &Change::Add {
            inputs: vec![add_file],
            folder: String::new(),
        },
        "",
        &op,
    )
    .unwrap();

    // Verify all parts are updated and readable
    let updated_archive = Archive::open(&first, "", &op).unwrap();
    assert!(updated_archive.writable);
    assert_eq!(updated_archive.entries.len(), 3);

    // Test archive integrity
    operations::test(&updated_archive, "", &op).unwrap();

    // Extract all files and verify exact byte content
    let out_dir = t.path().join("extract_add");
    operations::extract(
        &updated_archive,
        &ExtractOptions {
            destination: out_dir.clone(),
            selected: vec![],
            conflict: Conflict::Replace,
            smart: false,
            policy: Policy::default(),
        },
        "",
        &op,
    )
    .unwrap();

    assert_eq!(fs::read(out_dir.join("file1.bin")).unwrap(), data1);
    assert_eq!(fs::read(out_dir.join("file2.bin")).unwrap(), data2);
    assert_eq!(fs::read(out_dir.join("file3.bin")).unwrap(), data3);
}

#[test]
fn test_multivolume_7z_delete_shrinking() {
    let t = tempfile::tempdir().unwrap();
    let large_data = pseudo_random_bytes(160_000, 444);
    let small_data = b"small persistent content".to_vec();

    let (first, initial_parts) = create_split_7z(
        t.path(),
        "archive_del",
        &[("large.bin", &large_data), ("small.txt", &small_data)],
        65536,
    );
    assert!(
        initial_parts.len() >= 3,
        "Expected at least 3 parts for ~160KB with 64KB split"
    );

    let op = Operation::default();
    let archive = Archive::open(&first, "", &op).unwrap();
    assert!(archive.writable);
    assert_eq!(archive.entries.len(), 2);

    // Delete large.bin
    modify(
        &archive,
        &Change::Delete {
            names: vec!["large.bin".into()],
        },
        "",
        &op,
    )
    .unwrap();

    // Verify volume shrinking: only small.txt remains, which fits in 1 part
    let current_parts = existing_volume_parts(&first).unwrap();
    assert_eq!(
        current_parts.len(),
        1,
        "Expected archive to shrink to 1 part"
    );

    // Trailing parts (.002, .003) must be pruned from disk
    assert!(
        !t.path().join("archive_del.7z.002").exists(),
        "Part 2 must be pruned"
    );
    assert!(
        !t.path().join("archive_del.7z.003").exists(),
        "Part 3 must be pruned"
    );

    // Verify single remaining volume contains small.txt intact
    let updated_archive = Archive::open(&first, "", &op).unwrap();
    assert_eq!(updated_archive.entries.len(), 1);
    assert_eq!(updated_archive.entries[0].name, "small.txt");
    operations::test(&updated_archive, "", &op).unwrap();

    let out_dir = t.path().join("extract_del");
    operations::extract(
        &updated_archive,
        &ExtractOptions {
            destination: out_dir.clone(),
            selected: vec![],
            conflict: Conflict::Replace,
            smart: false,
            policy: Policy::default(),
        },
        "",
        &op,
    )
    .unwrap();
    assert_eq!(fs::read(out_dir.join("small.txt")).unwrap(), small_data);
}

#[test]
fn test_multivolume_7z_rename() {
    let t = tempfile::tempdir().unwrap();
    let data1 = pseudo_random_bytes(70_000, 555);
    let data2 = pseudo_random_bytes(70_000, 666);

    let (first, _) = create_split_7z(
        t.path(),
        "archive_ren",
        &[("old_name.bin", &data1), ("keep.bin", &data2)],
        65536,
    );

    let op = Operation::default();
    let archive = Archive::open(&first, "", &op).unwrap();
    assert!(archive.writable);

    modify(
        &archive,
        &Change::Rename {
            old: "old_name.bin".into(),
            new: "sub/renamed.bin".into(),
        },
        "",
        &op,
    )
    .unwrap();

    let updated_archive = Archive::open(&first, "", &op).unwrap();
    let names: Vec<_> = updated_archive
        .entries
        .iter()
        .map(|e| e.name.as_str())
        .collect();
    assert!(names.contains(&"sub/renamed.bin"));
    assert!(names.contains(&"keep.bin"));
    assert!(!names.contains(&"old_name.bin"));

    operations::test(&updated_archive, "", &op).unwrap();

    let out_dir = t.path().join("extract_ren");
    operations::extract(
        &updated_archive,
        &ExtractOptions {
            destination: out_dir.clone(),
            selected: vec![],
            conflict: Conflict::Replace,
            smart: false,
            policy: Policy::default(),
        },
        "",
        &op,
    )
    .unwrap();

    assert_eq!(fs::read(out_dir.join("sub/renamed.bin")).unwrap(), data1);
    assert_eq!(fs::read(out_dir.join("keep.bin")).unwrap(), data2);
}

#[test]
fn test_multivolume_7z_replace() {
    let t = tempfile::tempdir().unwrap();
    let data1 = pseudo_random_bytes(70_000, 777);
    let data2 = pseudo_random_bytes(70_000, 888);

    let (first, _) = create_split_7z(
        t.path(),
        "archive_rep",
        &[("target.bin", &data1), ("other.bin", &data2)],
        65536,
    );

    let op = Operation::default();
    let archive = Archive::open(&first, "", &op).unwrap();
    assert!(archive.writable);

    let new_data = pseudo_random_bytes(90_000, 999);
    let replacement_input = t.path().join("new_target.bin");
    fs::write(&replacement_input, &new_data).unwrap();

    let expected_archive_hash = operations::hash_file(&first, &op).unwrap();
    let expected_input_hash = operations::hash_file(&replacement_input, &op).unwrap();

    modify(
        &archive,
        &Change::Replace {
            name: "target.bin".into(),
            input: replacement_input,
            expected_archive_hash,
            expected_input_hash,
        },
        "",
        &op,
    )
    .unwrap();

    let updated_archive = Archive::open(&first, "", &op).unwrap();
    operations::test(&updated_archive, "", &op).unwrap();

    let out_dir = t.path().join("extract_rep");
    operations::extract(
        &updated_archive,
        &ExtractOptions {
            destination: out_dir.clone(),
            selected: vec![],
            conflict: Conflict::Replace,
            smart: false,
            policy: Policy::default(),
        },
        "",
        &op,
    )
    .unwrap();

    assert_eq!(fs::read(out_dir.join("target.bin")).unwrap(), new_data);
    assert_eq!(fs::read(out_dir.join("other.bin")).unwrap(), data2);
}

#[cfg(windows)]
#[test]
fn test_multivolume_rar_reports_writable() {
    let fixtures = Path::new(env!("CARGO_MANIFEST_DIR")).join("../tests/archives");
    let op = Operation::default();

    // 1. RAR5 multi-volume: should report writable = true
    let t_rar5 = tempfile::tempdir().unwrap();
    for i in 1..=4 {
        let name = format!("test_read_format_rar5_multiarchive_solid.part{i:02}.rar");
        fs::copy(fixtures.join(&name), t_rar5.path().join(&name)).unwrap();
    }
    let rar5_first = t_rar5
        .path()
        .join("test_read_format_rar5_multiarchive_solid.part01.rar");
    let rar5_archive = Archive::open(&rar5_first, "", &op).unwrap();
    assert!(
        rar5_archive.writable,
        "RAR5 multi-volume archive must report writable = true"
    );

    // 2. RAR4 multi-volume: should report writable = false (RAR4 rebuild not supported)
    let t_rar4 = tempfile::tempdir().unwrap();
    for i in 1..=3 {
        let name = format!("test_rar_multivolume_single_file.part{i}.rar");
        fs::copy(fixtures.join(&name), t_rar4.path().join(&name)).unwrap();
    }
    let rar4_first = t_rar4
        .path()
        .join("test_rar_multivolume_single_file.part1.rar");
    let rar4_archive = Archive::open(&rar4_first, "", &op).unwrap();
    assert!(
        !rar4_archive.writable,
        "RAR4 multi-volume archive must report writable = false"
    );
}
