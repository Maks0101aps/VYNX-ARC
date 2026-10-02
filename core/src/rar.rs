//! Isolated official UnRAR decoder adapter. Native code never receives output paths.
//! Uses only RAR_TEST and bounded callback streaming; RAR creation is impossible here.
use crate::{ArcError, Entry, Operation, Result};
use std::{
    io::{Cursor, Read},
    path::Path,
    ptr::NonNull,
    sync::mpsc::{self, Receiver, SyncSender},
};
use unrar_ng_sys as sys;
use zeroize::Zeroizing;

enum Event {
    Header(Entry),
    Chunk(Vec<u8>),
    FileDone(Result<()>),
    ArchiveDone,
}
struct Context {
    op: Operation,
    password: Zeroizing<Vec<u16>>,
    sender: Option<SyncSender<Event>>,
    bytes: u64,
    expected: u64,
}
extern "C" fn callback(
    message: sys::UINT,
    user: sys::LPARAM,
    p1: sys::LPARAM,
    p2: sys::LPARAM,
) -> i32 {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        if user == 0 {
            return -1;
        }
        // SAFETY: UnRAR invokes callbacks synchronously while the pinned Box<Context>
        // in Native is live; this pointer is registered only with its owning handle.
        let context = unsafe { &mut *(user as *mut Context) };
        if context.op.check().is_err() {
            return -1;
        }
        match message {
            sys::UCM_NEEDPASSWORDW => {
                if context.password.len() <= 1
                    || p1 == 0
                    || p2 <= 0
                    || context.password.len() > p2 as usize
                {
                    return -1;
                }
                // SAFETY: UCM_NEEDPASSWORDW supplies a writable wchar_t buffer of p2
                // elements; source is a live terminated UTF-16 vector checked above.
                unsafe {
                    std::ptr::copy_nonoverlapping(
                        context.password.as_ptr(),
                        p1 as *mut u16,
                        context.password.len(),
                    );
                }
                1
            }
            sys::UCM_PROCESSDATA => {
                if p1 == 0 || p2 < 0 || p2 as usize > 16 * 1024 * 1024 {
                    return -1;
                }
                let Some(bytes) = context.bytes.checked_add(p2 as u64) else {
                    return -1;
                };
                if bytes > context.expected {
                    return -1;
                }
                context.bytes = bytes;
                // SAFETY: Official UnRAR UCM_PROCESSDATA supplies a readable p2-byte
                // buffer valid for this callback only. No pointer escapes the callback.
                let data = unsafe { std::slice::from_raw_parts(p1 as *const u8, p2 as usize) };
                if let Some(tx) = &context.sender {
                    for chunk in data.chunks(128 * 1024) {
                        if tx.send(Event::Chunk(chunk.to_vec())).is_err() {
                            return -1;
                        }
                    }
                }
                1
            }
            sys::UCM_CHANGEVOLUME | sys::UCM_CHANGEVOLUMEW => {
                if p2 == sys::RAR_VOL_ASK {
                    -1
                } else {
                    0
                }
            }
            sys::UCM_LARGEDICT => 0,
            sys::UCM_NEEDPASSWORD => -1,
            _ => 0,
        }
    }))
    .unwrap_or(-1)
}

fn error(code: i32) -> ArcError {
    let message = match code {
        sys::ERAR_MISSING_PASSWORD => "A password is required",
        sys::ERAR_BAD_PASSWORD => "Incorrect password or damaged encrypted data",
        sys::ERAR_BAD_DATA => "RAR checksum mismatch or damaged data",
        sys::ERAR_LARGE_DICT => "RAR dictionary exceeds the decoder memory limit",
        sys::ERAR_EOPEN => "Could not open RAR archive or required volume",
        _ => "RAR decoding failed",
    };
    ArcError::new("RAR", format!("{message} (decoder code {code})"))
}
fn check(code: i32) -> Result<()> {
    if code == sys::ERAR_SUCCESS {
        Ok(())
    } else {
        Err(error(code))
    }
}

struct Native {
    handle: NonNull<sys::Handle>,
    context: Box<Context>,
    _filename: Vec<u16>,
}
impl Native {
    fn open(
        path: &Path,
        password: &str,
        op: &Operation,
        extract: bool,
        sender: Option<SyncSender<Event>>,
    ) -> Result<Self> {
        use std::os::windows::ffi::OsStrExt;
        let mut filename: Vec<u16> = path.as_os_str().encode_wide().collect();
        if filename.contains(&0) || password.contains('\0') {
            return Err(ArcError::new("PATH", "NUL in archive path or password"));
        }
        filename.push(0);
        let mut pw: Zeroizing<Vec<u16>> = Zeroizing::new(password.encode_utf16().collect());
        pw.push(0);
        let mut context = Box::new(Context {
            op: op.clone(),
            password: pw,
            sender,
            bytes: 0,
            expected: 0,
        });
        let mut data = sys::OpenArchiveDataEx::new(
            filename.as_ptr(),
            if extract {
                sys::RAR_OM_EXTRACT
            } else {
                sys::RAR_OM_LIST
            },
        );
        data.callback = Some(callback);
        data.user_data = (&mut *context as *mut Context) as sys::LPARAM;
        // SAFETY: data has the upstream packed ABI and all registered pointer backing
        // allocations remain live for this call and then the complete Native lifetime.
        let raw = unsafe { sys::RAROpenArchiveEx(&mut data) };
        let Some(handle) = NonNull::new(raw.cast_mut()) else {
            return Err(error(data.open_result as i32));
        };
        let native = Self {
            handle,
            context,
            _filename: filename,
        };
        check(data.open_result as i32)?;
        Ok(native)
    }
    fn next(&mut self, id: u64) -> Result<Option<Entry>> {
        self.context.op.check()?;
        let mut h = sys::HeaderDataEx::default();
        // SAFETY: handle is open and exclusively accessed by this thread; h is writable,
        // correctly initialized upstream ABI storage with no dangling auxiliary buffers.
        let code = unsafe { sys::RARReadHeaderEx(self.handle.as_ptr(), &mut h) };
        if code == sys::ERAR_END_ARCHIVE {
            return Ok(None);
        }
        check(code)?;
        let raw = h.filename_w;
        let end = raw
            .iter()
            .position(|c| *c == 0)
            .ok_or_else(|| ArcError::new("NAME", "RAR filename is too long"))?;
        let name = String::from_utf16(&raw[..end])
            .map_err(|_| ArcError::new("NAME", "Invalid UTF-16 RAR filename"))?;
        let size = ((h.unp_size_high as u64) << 32) | h.unp_size as u64;
        if h.dict_size > 512 * 1024 {
            return Err(ArcError::new("LIMIT", "RAR dictionary exceeds 512 MiB"));
        }
        let entry = Entry {
            id,
            name,
            size,
            packed: ((h.pack_size_high as u64) << 32) | h.pack_size as u64,
            directory: h.flags & sys::RHDF_DIRECTORY != 0,
            encrypted: h.flags & sys::RHDF_ENCRYPTED != 0,
            link: h.redir_type != 0
                || h.file_attr & 0x400 != 0
                || h.host_os == 3 && h.file_attr & 0xf000 == 0xa000,
            crc: Some(h.file_crc),
            modified: String::new(),
        };
        self.context.bytes = 0;
        self.context.expected = entry.size;
        Ok(Some(entry))
    }
    fn process(&mut self, test: bool) -> Result<()> {
        // SAFETY: handle is live and used synchronously. RAR_TEST never writes files;
        // null destination pointers are permitted in test/skip modes by UnRAR's API.
        let code = unsafe {
            sys::RARProcessFileW(
                self.handle.as_ptr(),
                if test { sys::RAR_TEST } else { sys::RAR_SKIP },
                std::ptr::null_mut(),
                std::ptr::null_mut(),
            )
        };
        self.context.op.check()?;
        check(code)
    }
}
impl Drop for Native {
    fn drop(&mut self) {
        // SAFETY: Native owns this open handle exactly once. UnRAR closes it before
        // the context and filename backing allocations are dropped.
        unsafe {
            sys::RARCloseArchive(self.handle.as_ptr());
        }
    }
}

pub fn list(path: &Path, password: &str, op: &Operation) -> Result<Vec<Entry>> {
    let mut native = Native::open(path, password, op, false, None)?;
    let mut entries = Vec::new();
    while let Some(entry) = native.next(entries.len() as u64)? {
        if entries.len() >= 1_000_000 {
            return Err(ArcError::new("LIMIT", "Too many RAR entries"));
        }
        entries.push(entry);
        native.process(false)?;
    }
    Ok(entries)
}

struct ChannelReader<'a> {
    rx: &'a Receiver<Event>,
    buffer: Cursor<Vec<u8>>,
    done: bool,
}
impl Read for ChannelReader<'_> {
    fn read(&mut self, out: &mut [u8]) -> std::io::Result<usize> {
        if out.is_empty() {
            return Ok(0);
        }
        loop {
            let n = self.buffer.read(out)?;
            if n > 0 {
                return Ok(n);
            }
            if self.done {
                return Ok(0);
            }
            match self.rx.recv().map_err(std::io::Error::other)? {
                Event::Chunk(bytes) => self.buffer = Cursor::new(bytes),
                Event::FileDone(result) => {
                    self.done = true;
                    result.map_err(std::io::Error::other)?;
                    return Ok(0);
                }
                _ => return Err(std::io::Error::other("Unexpected RAR stream event")),
            }
        }
    }
}
pub fn streams(
    path: &Path,
    password: &str,
    entries: &[Entry],
    op: &Operation,
    mut consume: impl FnMut(&Entry, &mut dyn Read) -> Result<()>,
) -> Result<()> {
    std::thread::scope(|scope| {
        let (tx, rx) = mpsc::sync_channel(2);
        scope.spawn(move || {
            let result = (|| {
                let mut native = Native::open(path, password, op, true, Some(tx.clone()))?;
                let mut index = 0;
                while let Some(entry) = native.next(index)? {
                    index += 1;
                    if entry.link {
                        return Err(ArcError::new("LINK", "RAR links are blocked"));
                    }
                    if tx.send(Event::Header(entry.clone())).is_err() {
                        return Ok(());
                    }
                    if !entry.directory {
                        let result = native.process(true);
                        let failed = result.is_err();
                        if tx.send(Event::FileDone(result)).is_err() || failed {
                            return Ok(());
                        }
                    } else {
                        native.process(false)?;
                    }
                }
                let _ = tx.send(Event::ArchiveDone);
                Ok(())
            })();
            if let Err(e) = result {
                let _ = tx.send(Event::FileDone(Err(e)));
            }
        });
        let mut count = 0usize;
        loop {
            match rx
                .recv()
                .map_err(|_| ArcError::new("RAR", "RAR worker ended unexpectedly"))?
            {
                Event::Header(actual) => {
                    let expected = entries
                        .get(count)
                        .ok_or_else(|| ArcError::new("CHANGED", "RAR entry count changed"))?;
                    count += 1;
                    if actual != *expected {
                        return Err(ArcError::new(
                            "CHANGED",
                            "RAR archive changed since opening",
                        ));
                    }
                    if !expected.directory {
                        let mut reader = ChannelReader {
                            rx: &rx,
                            buffer: Cursor::new(Vec::new()),
                            done: false,
                        };
                        consume(expected, &mut reader)?;
                        if !reader.done {
                            return Err(ArcError::new(
                                "RAR",
                                "Consumer did not finish decoding the entry",
                            ));
                        }
                    }
                }
                Event::ArchiveDone => {
                    if count != entries.len() {
                        return Err(ArcError::new("CHANGED", "RAR was truncated"));
                    }
                    return Ok(());
                }
                Event::FileDone(Err(e)) => return Err(e),
                _ => return Err(ArcError::new("RAR", "Invalid RAR stream sequence")),
            }
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn callback_bounds_and_cancellation() {
        assert_eq!(callback(sys::UCM_PROCESSDATA, 0, 0, 0), -1);
        let (tx, rx) = mpsc::sync_channel(2);
        let mut ctx = Box::new(Context {
            op: Operation::default(),
            password: Zeroizing::new(vec![0]),
            sender: Some(tx),
            bytes: 0,
            expected: 4,
        });
        let data = b"test";
        let ptr = (&mut *ctx as *mut Context) as sys::LPARAM;
        assert_eq!(
            callback(sys::UCM_PROCESSDATA, ptr, data.as_ptr() as sys::LPARAM, 4),
            1
        );
        assert!(matches!(rx.recv().unwrap(),Event::Chunk(v) if v==data));
        assert_eq!(
            callback(sys::UCM_PROCESSDATA, ptr, data.as_ptr() as sys::LPARAM, 1),
            -1
        );
        ctx.op.cancel();
        assert_eq!(
            callback(sys::UCM_PROCESSDATA, ptr, data.as_ptr() as sys::LPARAM, 0),
            -1
        );
    }
    #[test]
    fn password_callback_checks_capacity() {
        let mut ctx = Box::new(Context {
            op: Operation::default(),
            password: Zeroizing::new(vec![65, 66, 0]),
            sender: None,
            bytes: 0,
            expected: 0,
        });
        let mut out = [0u16; 4];
        let ptr = (&mut *ctx as *mut Context) as sys::LPARAM;
        assert_eq!(
            callback(
                sys::UCM_NEEDPASSWORDW,
                ptr,
                out.as_mut_ptr() as sys::LPARAM,
                2
            ),
            -1
        );
        assert_eq!(out, [0; 4]);
        assert_eq!(
            callback(
                sys::UCM_NEEDPASSWORDW,
                ptr,
                out.as_mut_ptr() as sys::LPARAM,
                4
            ),
            1
        );
        assert_eq!(out, [65, 66, 0, 0]);
    }
}
