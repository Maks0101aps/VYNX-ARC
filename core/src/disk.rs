use crate::{ArcError, Result};
use std::path::Path;

/// Conservative estimate, not a reservation. Every write still handles disk-full errors.
pub fn preflight(destination: &Path, required: u64) -> Result<()> {
    let margin = (required / 20).max(64 << 20);
    ensure_capacity(required, available(destination)?, margin)
}
pub fn ensure_capacity(required: u64, available: u64, margin: u64) -> Result<()> {
    let needed = required
        .checked_add(margin)
        .ok_or_else(|| ArcError::new("LIMIT", "Disk estimate overflow"))?;
    if available < needed {
        return Err(ArcError::new(
            "DISK_SPACE",
            format!(
                "Insufficient free space: need approximately {needed} bytes including safety margin; {available} bytes available"
            ),
        ));
    }
    Ok(())
}
#[cfg(windows)]
pub fn available(destination: &Path) -> Result<u64> {
    use std::os::windows::ffi::OsStrExt;
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn GetDiskFreeSpaceExW(
            path: *const u16,
            available: *mut u64,
            total: *mut u64,
            free: *mut u64,
        ) -> i32;
    }
    let mut parent = destination;
    while !parent.exists() {
        parent = parent
            .parent()
            .filter(|p| !p.as_os_str().is_empty())
            .unwrap_or(Path::new("."));
    }
    let absolute = parent.canonicalize()?;
    let path: Vec<u16> = absolute.as_os_str().encode_wide().chain(Some(0)).collect();
    let mut bytes = 0u64;
    // SAFETY: path is NUL-terminated and lives through this synchronous call;
    // bytes is a valid writable DWORDLONG and optional outputs are null.
    if unsafe {
        GetDiskFreeSpaceExW(
            path.as_ptr(),
            &mut bytes,
            std::ptr::null_mut(),
            std::ptr::null_mut(),
        )
    } == 0
    {
        return Err(std::io::Error::last_os_error().into());
    }
    Ok(bytes)
}
#[cfg(not(windows))]
pub fn available(_: &Path) -> Result<u64> {
    Err(ArcError::new(
        "PLATFORM",
        "Free-space preflight requires Windows",
    ))
}
#[cfg(test)]
mod tests {
    #[test]
    fn rejects_exhaustion_and_overflow() {
        assert!(super::ensure_capacity(100, 109, 10).is_err());
        assert!(super::ensure_capacity(u64::MAX, u64::MAX, 1).is_err());
        assert!(super::ensure_capacity(100, 110, 10).is_ok());
    }
}
