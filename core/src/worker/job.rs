//! Windows job lifetime and committed-memory cap; not a filesystem sandbox.
use crate::{ArcError, Result};
use std::{ffi::c_void, os::windows::io::AsRawHandle, process::Child};
#[repr(C)]
#[derive(Default)]
struct Basic {
    process_time: i64,
    job_time: i64,
    flags: u32,
    min: usize,
    max: usize,
    active: u32,
    affinity: usize,
    priority: u32,
    scheduling: u32,
}
#[repr(C)]
#[derive(Default)]
struct Extended {
    basic: Basic,
    io: [u64; 6],
    process_memory: usize,
    job_memory: usize,
    peak_process: usize,
    peak_job: usize,
}
#[link(name = "kernel32")]
unsafe extern "system" {
    fn CreateJobObjectW(attributes: *const c_void, name: *const u16) -> *mut c_void;
    fn SetInformationJobObject(job: *mut c_void, class: i32, info: *const c_void, size: u32)
    -> i32;
    fn AssignProcessToJobObject(job: *mut c_void, process: *mut c_void) -> i32;
    fn CloseHandle(handle: *mut c_void) -> i32;
    fn SetErrorMode(mode: u32) -> u32;
    fn CreateIoCompletionPort(
        file: *mut c_void,
        port: *mut c_void,
        key: usize,
        threads: u32,
    ) -> *mut c_void;
    fn GetQueuedCompletionStatus(
        port: *mut c_void,
        bytes: *mut u32,
        key: *mut usize,
        overlapped: *mut *mut c_void,
        timeout: u32,
    ) -> i32;
}
pub struct Job(*mut c_void, *mut c_void);
#[repr(C)]
struct Association {
    key: usize,
    port: *mut c_void,
}
impl Job {
    pub fn memory_exceeded(&self) -> bool {
        let mut message = 0;
        let mut key = 0;
        let mut process = std::ptr::null_mut();
        let mut hit = false;
        while unsafe { GetQueuedCompletionStatus(self.1, &mut message, &mut key, &mut process, 0) }
            != 0
        {
            hit |= message == 9 || message == 10;
        }
        hit
    }
    pub fn attach(child: &Child, memory: usize) -> Result<Self> {
        let job = Self(
            unsafe { CreateJobObjectW(std::ptr::null(), std::ptr::null()) },
            unsafe { CreateIoCompletionPort(-1isize as *mut c_void, std::ptr::null_mut(), 0, 1) },
        );
        if job.0.is_null() || job.1.is_null() {
            return Err(ArcError::new(
                "WORKER_LIMIT",
                std::io::Error::last_os_error().to_string(),
            ));
        }
        let association = Association {
            key: 1,
            port: job.1,
        };
        if unsafe {
            SetInformationJobObject(
                job.0,
                7,
                (&association as *const Association).cast(),
                size_of::<Association>() as u32,
            )
        } == 0
        {
            return Err(ArcError::new(
                "WORKER_LIMIT",
                "Cannot attach worker resource notifications",
            ));
        }
        let mut info = Extended::default();
        // KILL_ON_JOB_CLOSE | PROCESS_MEMORY | ACTIVE_PROCESS; one worker, no children.
        info.basic.flags = 0x2000 | 0x100 | 0x8;
        info.basic.active = 1;
        info.process_memory = memory;
        if unsafe {
            SetInformationJobObject(
                job.0,
                9,
                (&info as *const Extended).cast(),
                size_of::<Extended>() as u32,
            )
        } == 0
            || unsafe { AssignProcessToJobObject(job.0, child.as_raw_handle()) } == 0
        {
            return Err(ArcError::new(
                "WORKER_LIMIT",
                format!(
                    "Cannot enforce worker limits: {}",
                    std::io::Error::last_os_error()
                ),
            ));
        }
        Ok(job)
    }
}
impl Drop for Job {
    fn drop(&mut self) {
        for handle in [self.0, self.1] {
            if !handle.is_null() {
                unsafe {
                    CloseHandle(handle);
                }
            }
        }
    }
}
pub fn suppress_error_dialogs() {
    unsafe {
        SetErrorMode(0x1 | 0x2 | 0x8000);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::{
        io::{Read, Write},
        process::{Command, Stdio},
        time::{Duration, Instant},
    };
    #[test]
    #[ignore = "Only launched by memory_cap_and_lifetime with a private handshake"]
    fn allocation_child() {
        if std::env::var_os("VYNX_WORKER_LIMIT_TEST").is_none() {
            return;
        }
        suppress_error_dialogs();
        let mut byte = [0];
        std::io::stdin().read_exact(&mut byte).unwrap();
        #[link(name = "kernel32")]
        unsafe extern "system" {
            fn VirtualAlloc(
                address: *mut c_void,
                size: usize,
                allocation: u32,
                protection: u32,
            ) -> *mut c_void;
        }
        let memory = unsafe { VirtualAlloc(std::ptr::null_mut(), 512 << 20, 0x3000, 0x04) };
        std::process::exit(if memory.is_null() { 77 } else { 78 });
    }
    #[test]
    fn memory_cap_and_lifetime() {
        let mut child = Command::new(std::env::current_exe().unwrap())
            .args([
                "--ignored",
                "--exact",
                "worker::job::tests::allocation_child",
            ])
            .env("VYNX_WORKER_LIMIT_TEST", "1")
            .stdin(Stdio::piped())
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn()
            .unwrap();
        let job = Job::attach(&child, 256 << 20).unwrap();
        child.stdin.take().unwrap().write_all(&[1]).unwrap();
        assert_eq!(child.wait().unwrap().code(), Some(77));
        let start = Instant::now();
        let mut notification = false;
        while start.elapsed() < Duration::from_secs(2) {
            if job.memory_exceeded() {
                notification = true;
                break;
            }
            std::thread::sleep(Duration::from_millis(10));
        }
        assert!(
            notification,
            "memory-limit notification missing on this host"
        );
        let mut waiting = Command::new(std::env::current_exe().unwrap())
            .args([
                "--ignored",
                "--exact",
                "worker::job::tests::allocation_child",
            ])
            .env("VYNX_WORKER_LIMIT_TEST", "1")
            .stdin(Stdio::piped())
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn()
            .unwrap();
        let job = Job::attach(&waiting, 256 << 20).unwrap();
        assert!(waiting.try_wait().unwrap().is_none());
        drop(job);
        let start = Instant::now();
        loop {
            if waiting.try_wait().unwrap().is_some() {
                break;
            }
            if start.elapsed() > Duration::from_secs(2) {
                let _ = waiting.kill();
                let _ = waiting.wait();
                panic!("job close did not terminate its worker");
            }
            std::thread::sleep(Duration::from_millis(10));
        }
    }
}
