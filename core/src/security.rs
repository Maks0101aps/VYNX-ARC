use crate::{ArcError, Entry, Result};
use std::{
    collections::HashMap,
    fs,
    path::{Path, PathBuf},
};

#[derive(Clone, Debug)]
pub struct Policy {
    pub max_entries: usize,
    pub max_file_bytes: u64,
    pub max_total_bytes: u64,
    pub max_ratio: u64,
}
impl Default for Policy {
    fn default() -> Self {
        Self {
            max_entries: 1_000_000,
            max_file_bytes: 64 << 30,
            max_total_bytes: 256 << 30,
            max_ratio: 10_000,
        }
    }
}
fn unsafe_path(name: &str) -> ArcError {
    ArcError::new("UNSAFE_PATH", format!("Archive path is unsafe: {name:?}"))
}

/// Windows-aware portable validation, independent of the host filesystem.
pub fn validate_name(name: &str) -> Result<String> {
    if name.is_empty() || name.len() > 32_000 || name.starts_with(['/', '\\']) {
        return Err(unsafe_path(name));
    }
    let normalized = name.replace('\\', "/");
    let normalized = normalized.strip_suffix('/').unwrap_or(&normalized);
    for part in normalized.split('/') {
        if part.is_empty()
            || part == "."
            || part == ".."
            || part.ends_with([' ', '.'])
            || part.encode_utf16().count() > 255
            || part.chars().any(|c| {
                c < ' '
                    || c == '\u{7f}'
                    || "<>:\"|?*".contains(c)
                    || ('\u{202a}'..='\u{202e}').contains(&c)
                    || ('\u{2066}'..='\u{2069}').contains(&c)
            })
        {
            return Err(unsafe_path(name));
        }
        let base = part
            .split('.')
            .next()
            .unwrap_or("")
            .trim_end_matches(' ')
            .to_uppercase();
        let device = ["CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"].contains(&base.as_str())
            || (base.starts_with("COM") || base.starts_with("LPT"))
                && base.chars().count() == 4
                && "123456789¹²³".contains(base.chars().last().unwrap_or('0'));
        if device {
            return Err(unsafe_path(name));
        }
    }
    Ok(normalized.to_owned())
}

pub fn validate_plan(entries: &[Entry], packed: u64, policy: &Policy) -> Result<u64> {
    if entries.len() > policy.max_entries {
        return Err(ArcError::new("LIMIT", "Too many archive entries"));
    }
    let mut seen = HashMap::new();
    let mut directories: HashMap<String, String> = HashMap::new();
    let mut total = 0u64;
    for entry in entries {
        let name = validate_name(&entry.name)?;
        if entry.link {
            return Err(ArcError::new(
                "LINK",
                format!("Link extraction is blocked: {}", entry.name),
            ));
        }
        if entry.size > policy.max_file_bytes {
            return Err(ArcError::new(
                "LIMIT",
                "An entry exceeds the file size limit",
            ));
        }
        total = total
            .checked_add(entry.size)
            .ok_or_else(|| ArcError::new("LIMIT", "Archive size overflow"))?;
        let key = name.to_uppercase();
        if seen.insert(key.clone(), entry.directory).is_some() {
            return Err(ArcError::new(
                "COLLISION",
                format!("Duplicate or case-insensitive path collision: {name}"),
            ));
        }
        let parts: Vec<_> = name.split('/').collect();
        for i in 1..parts.len() + usize::from(entry.directory) {
            let prefix = parts[..i].join("/");
            if let Some(previous) = directories.insert(prefix.to_uppercase(), prefix.clone())
                && previous != prefix
            {
                return Err(ArcError::new("COLLISION", "Ambiguous directory spelling"));
            }
        }
    }
    for (key, directory) in &seen {
        if !directory && directories.contains_key(key) {
            return Err(ArcError::new(
                "COLLISION",
                "A file conflicts with a directory path",
            ));
        }
    }
    if total > policy.max_total_bytes {
        return Err(ArcError::new(
            "LIMIT",
            "Total unpacked size exceeds the configured limit",
        ));
    }
    if packed > 0 && total / packed > policy.max_ratio {
        return Err(ArcError::new(
            "RATIO",
            "Suspicious compression ratio; review limits before extracting",
        ));
    }
    Ok(total)
}

pub fn reject_reparse(path: &Path) -> Result<()> {
    match fs::symlink_metadata(path) {
        Ok(metadata) => {
            #[cfg(windows)]
            let linked = {
                use std::os::windows::fs::MetadataExt;
                metadata.file_attributes() & 0x400 != 0
            };
            #[cfg(not(windows))]
            let linked = metadata.file_type().is_symlink();
            if linked {
                return Err(ArcError::new(
                    "REPARSE",
                    format!("Refusing link/reparse point: {}", path.display()),
                ));
            }
        }
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => {}
        Err(e) => return Err(e.into()),
    }
    Ok(())
}

/// Pin existing directories against rename/replacement for the lifetime of a write.
/// Windows share flags deny deletion; OPEN_REPARSE_POINT inspects the object itself.
pub fn pin_ancestors(path: &Path) -> Result<Vec<fs::File>> {
    let mut handles = Vec::new();
    for ancestor in path.ancestors().collect::<Vec<_>>().into_iter().rev() {
        if ancestor.as_os_str().is_empty() {
            continue;
        }
        match fs::symlink_metadata(ancestor) {
            Ok(m) => {
                reject_reparse(ancestor)?;
                if !m.is_dir() {
                    continue;
                }
                #[cfg(windows)]
                {
                    use std::os::windows::fs::{MetadataExt, OpenOptionsExt};
                    let file = fs::OpenOptions::new()
                        .read(true)
                        .share_mode(3)
                        .custom_flags(0x02200000)
                        .open(ancestor)?;
                    if file.metadata()?.file_attributes() & 0x400 != 0 {
                        return Err(ArcError::new(
                            "REPARSE",
                            "Destination contains a reparse point",
                        ));
                    }
                    handles.push(file);
                }
                #[cfg(not(windows))]
                {
                    reject_reparse(ancestor)?;
                    handles.push(fs::File::open(ancestor)?);
                }
            }
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => {}
            Err(e) => return Err(e.into()),
        }
    }
    Ok(handles)
}

pub fn check_ancestors(path: &Path) -> Result<()> {
    for ancestor in path.ancestors() {
        reject_reparse(ancestor)?;
    }
    Ok(())
}
pub fn output_path(root: &Path, name: &str) -> Result<PathBuf> {
    let safe = validate_name(name)?;
    let path = root.join(safe);
    check_ancestors(&path)?;
    Ok(path)
}

pub fn smart_destination(base: &Path, archive_name: &str, entries: &[Entry]) -> Result<PathBuf> {
    let mut top: Option<String> = None;
    let mut one_folder = !entries.is_empty();
    for entry in entries {
        let name = validate_name(&entry.name)?;
        if !name.contains('/') && !entry.directory {
            one_folder = false;
        }
        let first = name.split('/').next().unwrap_or("").to_owned();
        if top.as_ref().is_some_and(|v| *v != first) {
            one_folder = false;
        }
        top.get_or_insert(first);
    }
    if one_folder {
        Ok(base.to_path_buf())
    } else {
        let name = validate_name(archive_name)?;
        if name.contains('/') {
            return Err(unsafe_path(archive_name));
        }
        Ok(base.join(name))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rejects_windows_escapes() {
        for bad in [
            "../evil",
            "..\\evil",
            "C:\\evil",
            "C:evil",
            "//server/share",
            "\\absolute",
            "a//b",
            "a/./b",
            "NUL",
            "CON.txt",
            "COM¹.txt",
            "LPT9",
            "file:stream",
            "a. /b",
            "a./b",
            "a /b",
            "a\0b",
            "a\u{202e}b",
            "a/../../b",
        ] {
            assert!(validate_name(bad).is_err(), "accepted {bad:?}");
        }
    }
    #[test]
    fn permits_unicode_and_normalizes_separators() {
        assert_eq!(validate_name("папка\\файл.txt").unwrap(), "папка/файл.txt");
        assert_eq!(validate_name("a/b/").unwrap(), "a/b");
    }
    #[test]
    fn checks_collisions_and_prefixes() {
        let entry = |name: &str| Entry {
            name: name.into(),
            ..Default::default()
        };
        for names in [["Readme", "README"], ["a", "a/b"], ["A/x", "a/y"]] {
            assert!(validate_plan(&names.map(entry), 10, &Policy::default()).is_err());
        }
    }
    #[test]
    fn smart_extract_has_no_redundant_folder() {
        let e = |name: &str| Entry {
            name: name.into(),
            ..Default::default()
        };
        assert_eq!(
            smart_destination(
                Path::new("out"),
                "project",
                &[e("project/a"), e("project/b")]
            )
            .unwrap(),
            Path::new("out")
        );
        assert_eq!(
            smart_destination(Path::new("out"), "photos", &[e("a"), e("b")]).unwrap(),
            Path::new("out/photos")
        );
    }
    #[test]
    fn checked_limits() {
        let e = Entry {
            name: "a".into(),
            size: 11,
            ..Default::default()
        };
        let p = Policy {
            max_file_bytes: 10,
            ..Default::default()
        };
        assert!(validate_plan(&[e], 1, &p).is_err());
    }
}
