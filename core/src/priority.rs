//! Temporary priority change of the calling worker only; never process-wide.
use crate::settings::ResourceMode;
pub struct WorkerPriority {
    previous: Option<i32>,
    _same_thread: std::marker::PhantomData<std::rc::Rc<()>>,
}
#[cfg(windows)]
#[link(name = "kernel32")]
unsafe extern "system" {
    fn GetCurrentThread() -> *mut std::ffi::c_void;
    fn GetThreadPriority(thread: *mut std::ffi::c_void) -> i32;
    fn SetThreadPriority(thread: *mut std::ffi::c_void, priority: i32) -> i32;
}
impl WorkerPriority {
    pub fn enter(mode: ResourceMode) -> Self {
        let mut result = Self {
            previous: None,
            _same_thread: std::marker::PhantomData,
        };
        #[cfg(windows)]
        if mode == ResourceMode::Eco {
            // Pseudo handle remains on the calling thread; the guard is not Send.
            unsafe {
                let thread = GetCurrentThread();
                let previous = GetThreadPriority(thread);
                if previous != i32::MAX && previous > -1 && SetThreadPriority(thread, -1) != 0 {
                    result.previous = Some(previous);
                }
            }
        }
        #[cfg(not(windows))]
        let _ = mode;
        result
    }
    pub fn description(&self) -> &'static str {
        if self.previous.is_some() {
            "worker priority below-normal (restored on exit)"
        } else {
            "worker priority unchanged"
        }
    }
}
impl Drop for WorkerPriority {
    fn drop(&mut self) {
        #[cfg(windows)]
        if let Some(previous) = self.previous {
            unsafe {
                SetThreadPriority(GetCurrentThread(), previous);
            }
        }
    }
}

#[cfg(all(test, windows))]
mod tests {
    use super::*;
    #[test]
    fn eco_priority_is_restored_after_worker_scope() {
        std::thread::spawn(|| unsafe {
            let original = GetThreadPriority(GetCurrentThread());
            assert_ne!(original, i32::MAX);
            {
                let _guard = WorkerPriority::enter(ResourceMode::Eco);
                assert!(GetThreadPriority(GetCurrentThread()) <= -1);
            }
            assert_eq!(GetThreadPriority(GetCurrentThread()), original);
        })
        .join()
        .unwrap();
    }
}
