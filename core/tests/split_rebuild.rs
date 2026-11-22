use std::{
    fs::{self, OpenOptions},
    path::Path,
};
use vynx_arc_core::{
    Operation,
    split_rebuild::{
        atomic_swap_volumes, detect_volume_size, existing_volume_parts, preflight_multivolume_space,
    },
};

#[test]
fn test_existing_volume_parts_7z() {
    let t = tempfile::tempdir().unwrap();
    let part1 = t.path().join("archive.7z.001");
    let part2 = t.path().join("archive.7z.002");
    let part3 = t.path().join("archive.7z.003");

    fs::write(&part1, vec![1u8; 70_000]).unwrap();
    fs::write(&part2, vec![2u8; 70_000]).unwrap();
    fs::write(&part3, vec![3u8; 10_000]).unwrap();

    let parts = existing_volume_parts(&part1).unwrap();
    assert_eq!(parts.len(), 3);
    assert_eq!(parts[0], part1);
    assert_eq!(parts[1], part2);
    assert_eq!(parts[2], part3);

    // Should also find all parts starting from part 2
    let parts_from_2 = existing_volume_parts(&part2).unwrap();
    assert_eq!(parts_from_2.len(), 3);
    assert_eq!(parts_from_2[0], part1);

    // Non-existent first part should error
    let missing = t.path().join("nonexistent.7z.001");
    let err = existing_volume_parts(&missing).unwrap_err();
    assert_eq!(err.code, "MISSING_VOLUME");
}

#[test]
fn test_existing_volume_parts_rar_fixtures() {
    let fixtures = Path::new(env!("CARGO_MANIFEST_DIR")).join("../tests/archives");

    // Unpadded: .part1.rar .. .part3.rar
    let t1 = tempfile::tempdir().unwrap();
    for i in 1..=3 {
        let name = format!("test_rar_multivolume_single_file.part{i}.rar");
        fs::copy(fixtures.join(&name), t1.path().join(&name)).unwrap();
    }
    let rar_first = t1.path().join("test_rar_multivolume_single_file.part1.rar");
    let parts = existing_volume_parts(&rar_first).unwrap();
    assert_eq!(parts.len(), 3);
    assert_eq!(parts[0], rar_first);
    assert_eq!(
        parts[1],
        t1.path().join("test_rar_multivolume_single_file.part2.rar")
    );
    assert_eq!(
        parts[2],
        t1.path().join("test_rar_multivolume_single_file.part3.rar")
    );

    // Padded: .part01.rar .. .part04.rar
    let t2 = tempfile::tempdir().unwrap();
    for i in 1..=4 {
        let name = format!("test_read_format_rar5_multiarchive_solid.part{i:02}.rar");
        fs::copy(fixtures.join(&name), t2.path().join(&name)).unwrap();
    }
    let rar_padded_first = t2
        .path()
        .join("test_read_format_rar5_multiarchive_solid.part01.rar");
    let parts_padded = existing_volume_parts(&rar_padded_first).unwrap();
    assert_eq!(parts_padded.len(), 4);
    assert_eq!(parts_padded[0], rar_padded_first);
    assert_eq!(
        parts_padded[3],
        t2.path()
            .join("test_read_format_rar5_multiarchive_solid.part04.rar")
    );
}

#[test]
fn test_detect_volume_size() {
    let t = tempfile::tempdir().unwrap();
    let part1 = t.path().join("archive.7z.001");

    // Part with 128 KiB: returns 128 KiB
    fs::write(&part1, vec![0u8; 128 * 1024]).unwrap();
    assert_eq!(detect_volume_size(&part1).unwrap(), 128 * 1024);

    // Part with 10 KiB (< 64 KiB): returns minimum 64 KiB
    fs::write(&part1, vec![0u8; 10 * 1024]).unwrap();
    assert_eq!(detect_volume_size(&part1).unwrap(), 64 * 1024);
}

#[test]
fn test_preflight_multivolume_space() {
    let t = tempfile::tempdir().unwrap();
    let part1 = t.path().join("archive.7z.001");
    let part2 = t.path().join("archive.7z.002");
    fs::write(&part1, vec![0u8; 70_000]).unwrap();
    fs::write(&part2, vec![0u8; 70_000]).unwrap();

    // Normal space preflight should succeed
    assert!(preflight_multivolume_space(&part1, 100_000).is_ok());

    // Impossibly large space requirement should fail with DISK_SPACE or LIMIT
    let err = preflight_multivolume_space(&part1, u64::MAX - 200_000).unwrap_err();
    assert!(err.code == "DISK_SPACE" || err.code == "LIMIT");
}

#[test]
fn test_atomic_swap_normal() {
    let t = tempfile::tempdir().unwrap();
    let part1 = t.path().join("archive.7z.001");
    let part2 = t.path().join("archive.7z.002");
    let part3 = t.path().join("archive.7z.003");

    fs::write(&part1, b"old part 1").unwrap();
    fs::write(&part2, b"old part 2").unwrap();
    fs::write(&part3, b"old part 3").unwrap();

    let staged1 = t.path().join("staged.001");
    let staged2 = t.path().join("staged.002");
    let staged3 = t.path().join("staged.003");

    fs::write(&staged1, b"new part 1 contents").unwrap();
    fs::write(&staged2, b"new part 2 contents").unwrap();
    fs::write(&staged3, b"new part 3 contents").unwrap();

    let op = Operation::default();
    atomic_swap_volumes(&part1, &[staged1, staged2, staged3], &op).unwrap();

    // Target parts must have new contents
    assert_eq!(fs::read(&part1).unwrap(), b"new part 1 contents");
    assert_eq!(fs::read(&part2).unwrap(), b"new part 2 contents");
    assert_eq!(fs::read(&part3).unwrap(), b"new part 3 contents");

    // No backup files should remain
    let entries: Vec<_> = fs::read_dir(t.path())
        .unwrap()
        .map(|e| e.unwrap().file_name().to_string_lossy().to_string())
        .collect();
    assert!(!entries.iter().any(|name| name.contains("vynx-bak")));
    assert_eq!(entries.len(), 3);
}

#[test]
fn test_atomic_swap_shrinking() {
    let t = tempfile::tempdir().unwrap();
    let part1 = t.path().join("archive.7z.001");
    let part2 = t.path().join("archive.7z.002");
    let part3 = t.path().join("archive.7z.003");
    let part4 = t.path().join("archive.7z.004");

    fs::write(&part1, b"old 1").unwrap();
    fs::write(&part2, b"old 2").unwrap();
    fs::write(&part3, b"old 3").unwrap();
    fs::write(&part4, b"old 4").unwrap();

    // Staging only 2 parts (shrinking from 4 to 2)
    let staged1 = t.path().join("staged.1");
    let staged2 = t.path().join("staged.2");
    fs::write(&staged1, b"shrunk 1").unwrap();
    fs::write(&staged2, b"shrunk 2").unwrap();

    let op = Operation::default();
    atomic_swap_volumes(&part1, &[staged1, staged2], &op).unwrap();

    assert_eq!(fs::read(&part1).unwrap(), b"shrunk 1");
    assert_eq!(fs::read(&part2).unwrap(), b"shrunk 2");
    // Parts 3 and 4 must be gone!
    assert!(!part3.exists());
    assert!(!part4.exists());

    let entries: Vec<_> = fs::read_dir(t.path())
        .unwrap()
        .map(|e| e.unwrap().file_name().to_string_lossy().to_string())
        .collect();
    assert!(!entries.iter().any(|name| name.contains("vynx-bak")));
    assert_eq!(entries.len(), 2);
}

#[test]
fn test_atomic_swap_growing() {
    let t = tempfile::tempdir().unwrap();
    let part1 = t.path().join("archive.part1.rar");
    let part2 = t.path().join("archive.part2.rar");

    fs::write(&part1, b"old rar 1").unwrap();
    fs::write(&part2, b"old rar 2").unwrap();

    // Staging 4 parts (growing from 2 to 4)
    let staged1 = t.path().join("staged.1");
    let staged2 = t.path().join("staged.2");
    let staged3 = t.path().join("staged.3");
    let staged4 = t.path().join("staged.4");
    fs::write(&staged1, b"new rar 1").unwrap();
    fs::write(&staged2, b"new rar 2").unwrap();
    fs::write(&staged3, b"new rar 3").unwrap();
    fs::write(&staged4, b"new rar 4").unwrap();

    let op = Operation::default();
    atomic_swap_volumes(&part1, &[staged1, staged2, staged3, staged4], &op).unwrap();

    let part3 = t.path().join("archive.part3.rar");
    let part4 = t.path().join("archive.part4.rar");

    assert_eq!(fs::read(&part1).unwrap(), b"new rar 1");
    assert_eq!(fs::read(&part2).unwrap(), b"new rar 2");
    assert_eq!(fs::read(&part3).unwrap(), b"new rar 3");
    assert_eq!(fs::read(&part4).unwrap(), b"new rar 4");

    let entries: Vec<_> = fs::read_dir(t.path())
        .unwrap()
        .map(|e| e.unwrap().file_name().to_string_lossy().to_string())
        .collect();
    assert!(!entries.iter().any(|name| name.contains("vynx-bak")));
    assert_eq!(entries.len(), 4);
}

#[test]
fn test_atomic_swap_rollback_on_lock() {
    let t = tempfile::tempdir().unwrap();
    let part1 = t.path().join("archive.7z.001");
    let part2 = t.path().join("archive.7z.002");
    let part3 = t.path().join("archive.7z.003");

    fs::write(&part1, b"original 1").unwrap();
    fs::write(&part2, b"original 2").unwrap();
    fs::write(&part3, b"original 3").unwrap();

    let staged1 = t.path().join("staged.1");
    let staged2 = t.path().join("staged.2");
    let staged3 = t.path().join("staged.3");
    fs::write(&staged1, b"new 1").unwrap();
    fs::write(&staged2, b"new 2").unwrap();
    fs::write(&staged3, b"new 3").unwrap();

    // Lock part2 exclusively on Windows so it cannot be renamed or deleted
    #[cfg(windows)]
    let _lock = {
        use std::os::windows::fs::OpenOptionsExt;
        OpenOptions::new()
            .read(true)
            .write(true)
            .share_mode(0)
            .open(&part2)
            .unwrap()
    };

    #[cfg(not(windows))]
    let mut perms = fs::metadata(&part2).unwrap().permissions();
    #[cfg(not(windows))]
    {
        perms.set_readonly(true);
        fs::set_permissions(&part2, perms).unwrap();
    }

    let op = Operation::default();
    let result = atomic_swap_volumes(&part1, &[staged1, staged2, staged3], &op);
    assert!(result.is_err());

    #[cfg(windows)]
    drop(_lock);

    // Original files must still exist with original content!
    assert_eq!(fs::read(&part1).unwrap(), b"original 1");
    assert_eq!(fs::read(&part2).unwrap(), b"original 2");
    assert_eq!(fs::read(&part3).unwrap(), b"original 3");

    // No leftover backup files
    let entries: Vec<_> = fs::read_dir(t.path())
        .unwrap()
        .map(|e| e.unwrap().file_name().to_string_lossy().to_string())
        .collect();
    assert!(!entries.iter().any(|name| name.contains("vynx-bak")));
}

#[test]
fn test_atomic_swap_cancellation() {
    let t = tempfile::tempdir().unwrap();
    let part1 = t.path().join("archive.7z.001");
    let part2 = t.path().join("archive.7z.002");

    fs::write(&part1, b"orig 1").unwrap();
    fs::write(&part2, b"orig 2").unwrap();

    let staged1 = t.path().join("staged.1");
    let staged2 = t.path().join("staged.2");
    fs::write(&staged1, b"new 1").unwrap();
    fs::write(&staged2, b"new 2").unwrap();

    let op = Operation::default();
    op.cancel();

    let err = atomic_swap_volumes(&part1, &[staged1, staged2], &op).unwrap_err();
    assert_eq!(err.code, "CANCELLED");

    // Originals must be intact
    assert_eq!(fs::read(&part1).unwrap(), b"orig 1");
    assert_eq!(fs::read(&part2).unwrap(), b"orig 2");
}

#[test]
fn test_atomic_swap_rollback_on_phase2_failure() {
    let t = tempfile::tempdir().unwrap();
    let part1 = t.path().join("archive.7z.001");
    let part2 = t.path().join("archive.7z.002");
    let part3 = t.path().join("archive.7z.003");

    fs::write(&part1, b"original 1").unwrap();
    fs::write(&part2, b"original 2").unwrap();
    fs::write(&part3, b"original 3").unwrap();

    let staged1 = t.path().join("staged.1");
    let staged2 = t.path().join("staged.2");
    let staged3 = t.path().join("staged.3");
    fs::write(&staged1, b"new 1").unwrap();
    fs::write(&staged2, b"new 2").unwrap();
    fs::write(&staged3, b"new 3").unwrap();

    // Lock staged2 on Windows so that Phase 2 rename fails on part 2
    #[cfg(windows)]
    let _lock = {
        use std::os::windows::fs::OpenOptionsExt;
        OpenOptions::new()
            .read(true)
            .write(true)
            .share_mode(0)
            .open(&staged2)
            .unwrap()
    };

    let op = Operation::default();
    let result = atomic_swap_volumes(&part1, &[staged1, staged2.clone(), staged3], &op);
    assert!(result.is_err());

    #[cfg(windows)]
    drop(_lock);

    // Rollback should have removed placed target 1 and restored all original parts
    assert_eq!(fs::read(&part1).unwrap(), b"original 1");
    assert_eq!(fs::read(&part2).unwrap(), b"original 2");
    assert_eq!(fs::read(&part3).unwrap(), b"original 3");

    // No leftover backups
    let entries: Vec<_> = fs::read_dir(t.path())
        .unwrap()
        .map(|e| e.unwrap().file_name().to_string_lossy().to_string())
        .collect();
    assert!(!entries.iter().any(|name| name.contains("vynx-bak")));
}

#[test]
fn test_gap_in_consecutive_parts() {
    let t = tempfile::tempdir().unwrap();
    let part1 = t.path().join("archive.7z.001");
    let part2 = t.path().join("archive.7z.002");
    // part 3 is skipped
    let part4 = t.path().join("archive.7z.004");

    fs::write(&part1, b"p1").unwrap();
    fs::write(&part2, b"p2").unwrap();
    fs::write(&part4, b"p4").unwrap();

    let parts = existing_volume_parts(&part1).unwrap();
    assert_eq!(parts.len(), 2);
    assert_eq!(parts[0], part1);
    assert_eq!(parts[1], part2);
}

#[test]
fn test_four_digit_7z_parts() {
    let t = tempfile::tempdir().unwrap();
    let part1 = t.path().join("big_archive.7z.0001");
    let part2 = t.path().join("big_archive.7z.0002");

    fs::write(&part1, vec![0u8; 70_000]).unwrap();
    fs::write(&part2, vec![0u8; 70_000]).unwrap();

    let parts = existing_volume_parts(&part1).unwrap();
    assert_eq!(parts.len(), 2);
    assert_eq!(parts[0], part1);
    assert_eq!(parts[1], part2);
}

#[test]
fn test_atomic_swap_invalid_arguments() {
    let t = tempfile::tempdir().unwrap();
    let part1 = t.path().join("archive.7z.001");
    fs::write(&part1, b"p1").unwrap();

    let op = Operation::default();
    // Empty staged parts
    assert_eq!(
        atomic_swap_volumes(&part1, &[], &op).unwrap_err().code,
        "OPTIONS"
    );

    // Staged part does not exist
    let missing = t.path().join("does_not_exist.tmp");
    assert_eq!(
        atomic_swap_volumes(&part1, &[missing], &op)
            .unwrap_err()
            .code,
        "IO"
    );
}
