//! Multi-volume archive split rebuild engine, transactional staging, and atomic two-phase swap.
use crate::{ArcError, Operation, Result, disk, security};
use std::{
    fs, io,
    path::{Path, PathBuf},
    sync::atomic::{AtomicU64, Ordering},
    time::SystemTime,
};

pub const MAX_PARTS: usize = 4096;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum VolumePattern {
    /// 7-Zip numbered volume: `{base}.{:0width$}` (e.g. `archive.7z.001`)
    SevenZ { base: String, width: usize },
    /// RAR numbered volume: `{base}.{part_tag}{:0width$}{ext}` (e.g. `archive.part1.rar`, `archive.part01.rar`)
    Rar {
        base: String,
        part_tag: String,
        width: usize,
        ext: String,
    },
}

impl VolumePattern {
    pub fn format_name(&self, index: usize) -> String {
        match self {
            Self::SevenZ { base, width } => {
                format!("{base}.{index:0width$}")
            }
            Self::Rar {
                base,
                part_tag,
                width,
                ext,
            } => {
                format!("{base}.{part_tag}{index:0width$}{ext}")
            }
        }
    }

    pub fn part_path(&self, first: &Path, index: usize) -> PathBuf {
        let name = self.format_name(index);
        first.with_file_name(name)
    }
}

/// Detects the volume pattern and index of a multi-volume archive component path.
pub fn detect_volume_pattern(path: &Path) -> Option<(VolumePattern, usize)> {
    let file_name = path.file_name()?.to_str()?;

    // 1. Check 7Z: {base}.{digits} where base ends with .7z (case-insensitive) and digits.len() >= 3
    if let Some((base, number)) = file_name.rsplit_once('.')
        && base.to_ascii_lowercase().ends_with(".7z")
        && number.len() >= 3
        && number.bytes().all(|b| b.is_ascii_digit())
        && let Ok(idx) = number.parse::<usize>()
        && idx > 0
    {
        return Some((
            VolumePattern::SevenZ {
                base: base.to_string(),
                width: number.len(),
            },
            idx,
        ));
    }

    // 2. Check RAR: {base}.{part_tag}{digits}{ext} where ext is .rar (case-insensitive)
    let lower = file_name.to_ascii_lowercase();
    if lower.ends_with(".rar") {
        let ext_len = 4;
        let ext = &file_name[file_name.len() - ext_len..];
        let stem = &file_name[..file_name.len() - ext_len];
        if let Some((base, part_str)) = stem.rsplit_once('.')
            && part_str.to_ascii_lowercase().starts_with("part")
        {
            let digits = &part_str[4..];
            if !digits.is_empty()
                && digits.bytes().all(|b| b.is_ascii_digit())
                && let Ok(idx) = digits.parse::<usize>()
                && idx > 0
            {
                let part_tag = &part_str[..4];
                return Some((
                    VolumePattern::Rar {
                        base: base.to_string(),
                        part_tag: part_tag.to_string(),
                        width: digits.len(),
                        ext: ext.to_string(),
                    },
                    idx,
                ));
            }
        }
    }

    None
}

/// Returns the path to the first part (part 1) of a multi-volume archive set.
pub fn volume_first_part(path: &Path) -> Option<PathBuf> {
    let (pattern, _) = detect_volume_pattern(path)?;
    Some(pattern.part_path(path, 1))
}

/// Lists all existing consecutive volume parts in numerical order.
/// Supports both 7Z (`.7z.001` .. `.7z.NNN`) and RAR (`.part1.rar` .. `.partN.rar` and padded variants).
pub fn existing_volume_parts(first: &Path) -> Result<Vec<PathBuf>> {
    let (pattern, _) = detect_volume_pattern(first).ok_or_else(|| {
        ArcError::new(
            "FORMAT",
            format!("Not a recognized multi-volume archive: {}", first.display()),
        )
    })?;

    let part1 = pattern.part_path(first, 1);
    if !part1.exists() {
        return Err(ArcError::new(
            "MISSING_VOLUME",
            format!("Archive part is missing: {}", part1.display()),
        ));
    }

    let mut parts = Vec::new();
    for index in 1..=MAX_PARTS {
        let path = pattern.part_path(first, index);
        if path.is_file() {
            parts.push(path);
        } else if index == 1 {
            return Err(ArcError::new(
                "MISSING_VOLUME",
                format!("Archive part is missing: {}", path.display()),
            ));
        } else {
            break;
        }
    }

    Ok(parts)
}

/// Returns the split volume payload size (size of the first part, minimum 64 KiB).
pub fn detect_volume_size(first: &Path) -> Result<u64> {
    let (pattern, _) = detect_volume_pattern(first).ok_or_else(|| {
        ArcError::new(
            "FORMAT",
            format!("Not a recognized multi-volume archive: {}", first.display()),
        )
    })?;

    let part1 = pattern.part_path(first, 1);
    let meta = fs::metadata(&part1).map_err(|e| {
        if e.kind() == io::ErrorKind::NotFound {
            ArcError::new(
                "MISSING_VOLUME",
                format!("Archive part is missing: {}", part1.display()),
            )
        } else {
            ArcError::from(e)
        }
    })?;

    if !meta.is_file() {
        return Err(ArcError::new(
            "IO",
            format!("Volume part is not a regular file: {}", part1.display()),
        ));
    }

    // Minimum payload size for split volumes is 64 KiB
    let size = meta.len().max(64 * 1024);
    Ok(size)
}

/// Calculates total size of all existing volumes + needed_bytes and ensures available
/// disk space with 15% safety margin using `crate::disk`.
pub fn preflight_multivolume_space(first: &Path, needed_bytes: u64) -> Result<()> {
    let parts = existing_volume_parts(first)?;
    let mut existing_total: u64 = 0;
    for p in &parts {
        let len = fs::metadata(p)?.len();
        existing_total = existing_total
            .checked_add(len)
            .ok_or_else(|| ArcError::new("LIMIT", "Archive volume size overflow"))?;
    }

    let total_required = existing_total
        .checked_add(needed_bytes)
        .ok_or_else(|| ArcError::new("LIMIT", "Required space overflow"))?;

    // 15% safety margin
    let margin = ((total_required as u128 * 15) / 100) as u64;

    let target_dir = first
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or(Path::new("."));
    let available = disk::available(target_dir)?;
    disk::ensure_capacity(total_required, available, margin)
}

static COUNTER: AtomicU64 = AtomicU64::new(1);

fn generate_backup_id() -> String {
    use sha2::{Digest, Sha256};
    let nanos = SystemTime::now()
        .duration_since(SystemTime::UNIX_EPOCH)
        .map(|d| d.as_nanos())
        .unwrap_or(0);
    let count = COUNTER.fetch_add(1, Ordering::Relaxed);
    let pid = std::process::id();

    let mut hasher = Sha256::new();
    hasher.update(nanos.to_le_bytes());
    hasher.update(count.to_le_bytes());
    hasher.update(pid.to_le_bytes());
    let hash = hasher.finalize();

    format!(
        "{:08x}-{:04x}-{:04x}-{:04x}-{:012x}",
        u32::from_be_bytes(hash[0..4].try_into().unwrap()),
        u16::from_be_bytes(hash[4..6].try_into().unwrap()),
        (u16::from_be_bytes(hash[6..8].try_into().unwrap()) & 0x0fff) | 0x4000,
        (u16::from_be_bytes(hash[8..10].try_into().unwrap()) & 0x3fff) | 0x8000,
        u64::from_be_bytes(hash[10..18].try_into().unwrap()) & 0x0000_ffff_ffff_ffff
    )
}

#[cfg(windows)]
pub fn atomic_rename(from: &Path, to: &Path) -> io::Result<()> {
    use std::os::windows::ffi::OsStrExt;
    const MOVEFILE_REPLACE_EXISTING: u32 = 0x1;
    const MOVEFILE_WRITE_THROUGH: u32 = 0x8;
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn MoveFileExW(
            lpExistingFileName: *const u16,
            lpNewFileName: *const u16,
            dwFlags: u32,
        ) -> i32;
    }
    let from_w: Vec<u16> = from.as_os_str().encode_wide().chain(Some(0)).collect();
    let to_w: Vec<u16> = to.as_os_str().encode_wide().chain(Some(0)).collect();
    let ok = unsafe {
        MoveFileExW(
            from_w.as_ptr(),
            to_w.as_ptr(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH,
        )
    };
    if ok == 0 {
        Err(io::Error::last_os_error())
    } else {
        Ok(())
    }
}

#[cfg(not(windows))]
pub fn atomic_rename(from: &Path, to: &Path) -> io::Result<()> {
    fs::rename(from, to)
}

/// Performs a robust 3-phase atomic swap for multi-volume archives:
/// - Phase 1: Rename existing old parts to `.vynx-bak-{uuid}`. On failure, rollback renamed parts and return error.
/// - Phase 2: Move/rename staged parts to their target volume names. On failure, remove placed targets, restore all backups, and return error.
/// - Phase 3: Verify all target parts in place and delete all backup files.
pub fn atomic_swap_volumes(first: &Path, staged_parts: &[PathBuf], op: &Operation) -> Result<()> {
    op.check()?;
    if staged_parts.is_empty() {
        return Err(ArcError::new("OPTIONS", "No staged volume parts provided"));
    }

    let (pattern, _) = detect_volume_pattern(first).ok_or_else(|| {
        ArcError::new(
            "FORMAT",
            format!("Not a recognized multi-volume archive: {}", first.display()),
        )
    })?;

    // Verify all staged parts exist before touching any original files
    for staged in staged_parts {
        if !staged.is_file() {
            return Err(ArcError::new(
                "IO",
                format!("Staged volume part does not exist: {}", staged.display()),
            ));
        }
    }

    // Determine target parts for the staged parts
    let target_parts: Vec<PathBuf> = (1..=staged_parts.len())
        .map(|i| pattern.part_path(first, i))
        .collect();

    for target in &target_parts {
        security::check_ancestors(target)?;
    }

    let old_parts = existing_volume_parts(first)?;
    for old in &old_parts {
        security::check_ancestors(old)?;
    }

    let backup_id = generate_backup_id();
    let mut renamed_to_backup: Vec<(PathBuf, PathBuf)> = Vec::new();

    // Phase 1: Rename existing old parts to .vynx-bak-{uuid}
    op.phase("Backing up existing volume parts");
    for old_path in &old_parts {
        if let Err(e) = op.check() {
            // Rollback already renamed backups
            for (original, backup) in renamed_to_backup.iter().rev() {
                let _ = atomic_rename(backup, original);
            }
            return Err(e);
        }

        let old_file_name = old_path
            .file_name()
            .and_then(|s| s.to_str())
            .unwrap_or("volume");
        let backup_path = old_path.with_file_name(format!("{old_file_name}.vynx-bak-{backup_id}"));

        if let Err(err) = atomic_rename(old_path, &backup_path) {
            // Rollback any parts that were renamed in Phase 1
            for (original, backup) in renamed_to_backup.iter().rev() {
                let _ = atomic_rename(backup, original);
            }
            return Err(ArcError::new(
                "IO",
                format!(
                    "Failed to rename volume part {} to backup {}: {err}",
                    old_path.display(),
                    backup_path.display()
                ),
            ));
        }
        renamed_to_backup.push((old_path.clone(), backup_path));
    }

    // Phase 2: Move/rename staged parts to their final target volume names
    op.phase("Moving staged volume parts into place");
    let mut placed_targets: Vec<PathBuf> = Vec::new();
    let mut phase2_failed = false;
    let mut phase2_error = None;

    for (staged, target) in staged_parts.iter().zip(&target_parts) {
        if let Err(e) = op.check() {
            phase2_failed = true;
            phase2_error = Some(e);
            break;
        }

        if let Err(err) = atomic_rename(staged, target) {
            phase2_failed = true;
            phase2_error = Some(ArcError::new(
                "IO",
                format!(
                    "Failed to move staged volume part {} to target {}: {err}",
                    staged.display(),
                    target.display()
                ),
            ));
            break;
        }
        placed_targets.push(target.clone());
    }

    if phase2_failed {
        // Rollback Phase 2:
        // 1. Remove any target parts already placed in Phase 2
        for target in &placed_targets {
            let _ = fs::remove_file(target);
        }
        // 2. Restore all backups to original names
        for (original, backup) in renamed_to_backup.iter().rev() {
            let _ = atomic_rename(backup, original);
        }
        return Err(phase2_error.unwrap_or_else(|| ArcError::new("IO", "Phase 2 failed")));
    }

    // Phase 3: Verify all target parts in place and delete backups
    op.phase("Finalizing multi-volume archive");
    for target in &target_parts {
        if !target.is_file() {
            // Verification failed: rollback
            for t in &target_parts {
                let _ = fs::remove_file(t);
            }
            for (original, backup) in renamed_to_backup.iter().rev() {
                let _ = atomic_rename(backup, original);
            }
            return Err(ArcError::new(
                "VERIFY",
                format!("Committed volume part is missing: {}", target.display()),
            ));
        }
    }

    // All target parts verified in place: delete backup files
    for (_original, backup) in &renamed_to_backup {
        if backup.exists() {
            let _ = fs::remove_file(backup);
        }
    }

    Ok(())
}
