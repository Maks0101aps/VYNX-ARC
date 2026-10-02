//! ZIP DOS timestamps are local wall time; 7Z/TAR timestamps have absolute epochs.
#[cfg(windows)]
pub fn zip_unix(date: zip::DateTime) -> Option<u64> {
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn TzSpecificLocalTimeToSystemTime(
            zone: *const std::ffi::c_void,
            local: *const [u16; 8],
            utc: *mut [u16; 8],
        ) -> i32;
        fn SystemTimeToFileTime(time: *const [u16; 8], file: *mut u64) -> i32;
    }
    if !date.is_valid() {
        return None;
    }
    let local = [
        date.year(),
        date.month() as u16,
        0,
        date.day() as u16,
        date.hour() as u16,
        date.minute() as u16,
        date.second() as u16,
        0,
    ];
    let mut utc = [0u16; 8];
    let mut file = 0u64;
    // SAFETY: SYSTEMTIME is eight aligned WORDs in this exact field order. The
    // supplied pointers live through synchronous calls; null selects system TZ.
    if unsafe { TzSpecificLocalTimeToSystemTime(std::ptr::null(), &local, &mut utc) } == 0 {
        return None;
    }
    // SAFETY: valid writable FILETIME (two DWORDs), aligned to at least DWORD.
    if unsafe { SystemTimeToFileTime(&utc, &mut file) } == 0 {
        return None;
    }
    nt_unix(file)
}
#[cfg(not(windows))]
pub fn zip_unix(_: zip::DateTime) -> Option<u64> {
    None
}
pub fn nt_unix(time: u64) -> Option<u64> {
    time.checked_sub(116_444_736_000_000_000)
        .map(|t| t / 10_000_000)
}
