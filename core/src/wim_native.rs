//! Safe dynamic loading and bindings for Windows Imaging API (`wimgapi.dll`).

use crate::{ArcError, Result, operations::Operation, security};
use std::path::Path;

/// Handle to a WIM file or image object.
pub type WimHandle = *mut std::ffi::c_void;

/// Signature for `WIMCreateFile`.
pub type FnWimCreateFile = unsafe extern "system" fn(
    psz_wim_path: *const u16,
    dw_desired_access: u32,
    dw_creation_disposition: u32,
    dw_flags_and_attributes: u32,
    dw_compression_type: u32,
    pdw_creation_result: *mut u32,
) -> WimHandle;

/// Signature for `WIMCloseHandle`.
pub type FnWimCloseHandle = unsafe extern "system" fn(h_object: WimHandle) -> i32;

/// Signature for `WIMGetImageCount`.
pub type FnWimGetImageCount = unsafe extern "system" fn(h_wim: WimHandle) -> u32;

/// Signature for `WIMLoadImage`.
pub type FnWimLoadImage = unsafe extern "system" fn(h_wim: WimHandle, dw_image_index: u32) -> WimHandle;

/// Signature for `WIMApplyImage` / `WIMExtractImage`.
pub type FnWimExtractImage = unsafe extern "system" fn(
    h_image: WimHandle,
    psz_path: *const u16,
    dw_extract_flags: u32,
) -> i32;

/// Alias for `FnWimExtractImage` matching native `wimgapi.dll` naming.
pub type FnWimApplyImage = FnWimExtractImage;

/// Dynamically loaded `wimgapi.dll` function table.
pub struct WimApi {
    pub create_file: FnWimCreateFile,
    pub close_handle: FnWimCloseHandle,
    pub get_image_count: FnWimGetImageCount,
    pub load_image: FnWimLoadImage,
    pub extract_image: FnWimExtractImage,
    #[cfg(windows)]
    module: *mut std::ffi::c_void,
}

unsafe impl Send for WimApi {}
unsafe impl Sync for WimApi {}

#[cfg(windows)]
#[link(name = "kernel32")]
unsafe extern "system" {
    fn LoadLibraryW(lpLibFileName: *const u16) -> *mut std::ffi::c_void;
    fn FreeLibrary(hLibModule: *mut std::ffi::c_void) -> i32;
    fn GetProcAddress(
        hModule: *mut std::ffi::c_void,
        lpProcName: *const std::ffi::c_char,
    ) -> *mut std::ffi::c_void;
}

#[cfg(windows)]
impl WimApi {
    /// Dynamically load `wimgapi.dll` from `%SystemRoot%\System32\wimgapi.dll`.
    pub fn load() -> Result<Self> {
        let sys_root = std::env::var("SystemRoot").unwrap_or_else(|_| "C:\\Windows".to_string());
        let dll_path = format!("{}\\System32\\wimgapi.dll", sys_root);
        let wide: Vec<u16> = dll_path.encode_utf16().chain(std::iter::once(0)).collect();

        let module = unsafe { LoadLibraryW(wide.as_ptr()) };
        if module.is_null() {
            return Err(ArcError::new(
                "UNSUPPORTED",
                format!("Failed to load wimgapi.dll from {dll_path}"),
            ));
        }

        unsafe fn get_proc(
            module: *mut std::ffi::c_void,
            name: &[u8],
        ) -> Option<*mut std::ffi::c_void> {
            let ptr = unsafe { GetProcAddress(module, name.as_ptr() as *const std::ffi::c_char) };
            if ptr.is_null() { None } else { Some(ptr) }
        }

        unsafe {
            let create_file = get_proc(module, b"WIMCreateFile\0").ok_or_else(|| {
                ArcError::new("UNSUPPORTED", "Missing WIMCreateFile in wimgapi.dll")
            })?;
            let close_handle = get_proc(module, b"WIMCloseHandle\0").ok_or_else(|| {
                ArcError::new("UNSUPPORTED", "Missing WIMCloseHandle in wimgapi.dll")
            })?;
            let get_image_count = get_proc(module, b"WIMGetImageCount\0").ok_or_else(|| {
                ArcError::new("UNSUPPORTED", "Missing WIMGetImageCount in wimgapi.dll")
            })?;
            let load_image = get_proc(module, b"WIMLoadImage\0").ok_or_else(|| {
                ArcError::new("UNSUPPORTED", "Missing WIMLoadImage in wimgapi.dll")
            })?;
            let extract_image = get_proc(module, b"WIMApplyImage\0")
                .or_else(|| get_proc(module, b"WIMExtractImage\0"))
                .ok_or_else(|| {
                    ArcError::new(
                        "UNSUPPORTED",
                        "Missing WIMApplyImage/WIMExtractImage in wimgapi.dll",
                    )
                })?;

            Ok(Self {
                create_file: std::mem::transmute::<*mut std::ffi::c_void, FnWimCreateFile>(
                    create_file,
                ),
                close_handle: std::mem::transmute::<*mut std::ffi::c_void, FnWimCloseHandle>(
                    close_handle,
                ),
                get_image_count: std::mem::transmute::<*mut std::ffi::c_void, FnWimGetImageCount>(
                    get_image_count,
                ),
                load_image: std::mem::transmute::<*mut std::ffi::c_void, FnWimLoadImage>(
                    load_image,
                ),
                extract_image: std::mem::transmute::<*mut std::ffi::c_void, FnWimExtractImage>(
                    extract_image,
                ),
                module,
            })
        }
    }
}

#[cfg(windows)]
impl Drop for WimApi {
    fn drop(&mut self) {
        if !self.module.is_null() {
            unsafe {
                FreeLibrary(self.module);
            }
        }
    }
}

#[cfg(not(windows))]
impl WimApi {
    pub fn load() -> Result<Self> {
        Err(ArcError::new(
            "UNSUPPORTED",
            "wimgapi.dll is only available on Windows",
        ))
    }
}

/// RAII guard to safely close a `WimHandle` on drop.
pub struct WimHandleGuard<'a> {
    pub handle: WimHandle,
    pub api: &'a WimApi,
}

impl Drop for WimHandleGuard<'_> {
    fn drop(&mut self) {
        if !self.handle.is_null() {
            unsafe {
                (self.api.close_handle)(self.handle);
            }
            self.handle = std::ptr::null_mut();
        }
    }
}

#[cfg(windows)]
fn path_to_wide(path: &Path) -> Result<Vec<u16>> {
    let abs = std::path::absolute(path)?;
    let s = abs.to_string_lossy();
    let clean = s.strip_prefix(r"\\?\").unwrap_or(&s);
    let wide: Vec<u16> = clean.encode_utf16().chain(std::iter::once(0)).collect();
    Ok(wide)
}

/// Extract an image from a WIM archive to a destination directory using `wimgapi.dll`.
pub fn extract_image(
    wim_path: &Path,
    image_index: u32,
    destination: &Path,
    op: &Operation,
) -> Result<()> {
    op.check()?;
    security::check_ancestors(wim_path)?;
    security::check_ancestors(destination)?;
    std::fs::create_dir_all(destination)?;

    #[cfg(windows)]
    {
        let api = WimApi::load()?;
        op.check()?;

        let wim_w = path_to_wide(wim_path)?;
        // dwDesiredAccess = 0x8000_0000 (GENERIC_READ / WIM_GENERIC_READ)
        // dwCreationDisposition = 3 (OPEN_EXISTING / WIM_OPEN_EXISTING)
        let raw_wim = unsafe {
            (api.create_file)(
                wim_w.as_ptr(),
                0x8000_0000,
                3,
                0,
                0,
                std::ptr::null_mut(),
            )
        };
        if raw_wim.is_null() {
            return Err(ArcError::new(
                "CORRUPT",
                format!(
                    "Failed to open WIM file: {} ({})",
                    wim_path.display(),
                    std::io::Error::last_os_error()
                ),
            ));
        }
        let h_wim = WimHandleGuard {
            handle: raw_wim,
            api: &api,
        };

        op.check()?;
        let count = unsafe { (api.get_image_count)(h_wim.handle) };
        if image_index == 0 || image_index > count {
            return Err(ArcError::new(
                "SELECTION",
                format!("Invalid WIM image index: {image_index} (total images: {count})"),
            ));
        }

        let raw_img = unsafe { (api.load_image)(h_wim.handle, image_index) };
        if raw_img.is_null() {
            return Err(ArcError::new(
                "CORRUPT",
                format!(
                    "Failed to load WIM image index {image_index}: {}",
                    std::io::Error::last_os_error()
                ),
            ));
        }
        let h_img = WimHandleGuard {
            handle: raw_img,
            api: &api,
        };

        op.check()?;
        let dest_w = path_to_wide(destination)?;
        let ret = unsafe { (api.extract_image)(h_img.handle, dest_w.as_ptr(), 0) };
        if ret == 0 {
            return Err(ArcError::new(
                "CORRUPT",
                format!(
                    "Failed to extract WIM image index {image_index} to {}: {}",
                    destination.display(),
                    std::io::Error::last_os_error()
                ),
            ));
        }

        op.check()?;
        Ok(())
    }

    #[cfg(not(windows))]
    {
        let _ = (wim_path, image_index, destination, op);
        Err(ArcError::new(
            "UNSUPPORTED",
            "WIM extraction requires Windows wimgapi.dll",
        ))
    }
}
