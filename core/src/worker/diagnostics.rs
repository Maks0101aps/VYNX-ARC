//! Opt-in acceptance hooks, compiled out of normal packages.
use super::{Request, TEST};
use crate::{ArcError, Result};
pub(crate) fn matches(path: &std::path::Path, mode: &str) -> bool {
    std::env::var("VYNX_WORKER_FAULT").as_deref() == Ok(mode)
        && std::env::var_os("VYNX_WORKER_FAULT_TARGET").is_some_and(|p| p == path)
}
pub(super) fn before(request: &Request) -> Result<()> {
    if request.kind != TEST {
        return Ok(());
    }
    let path = std::path::Path::new(&request.path);
    if matches(path, "crash") {
        std::process::abort();
    }
    if matches(path, "hang") {
        loop {
            std::thread::park();
        }
    }
    #[cfg(windows)]
    if matches(path, "memory") {
        use std::ffi::c_void;
        #[link(name = "kernel32")]
        unsafe extern "system" {
            fn VirtualAlloc(
                address: *mut c_void,
                size: usize,
                allocation: u32,
                protection: u32,
            ) -> *mut c_void;
            fn VirtualFree(address: *mut c_void, size: usize, kind: u32) -> i32;
        }
        let memory = unsafe { VirtualAlloc(std::ptr::null_mut(), 1 << 30, 0x3000, 0x04) };
        if memory.is_null() {
            return Err(ArcError::new(
                "WORKER_MEMORY",
                "Diagnostic 1 GiB committed allocation was refused by the worker job",
            ));
        }
        unsafe {
            VirtualFree(memory, 0, 0x8000);
        }
        return Err(ArcError::new(
            "INTERNAL",
            "Diagnostic allocation unexpectedly exceeded the Eco budget",
        ));
    }
    Ok(())
}
