//! Owned archive worker. The CLI executes core operations directly; GUI uses IPC.
#[cfg(windows)]
mod job;
mod wire;
use crate::{ArcError, Archive, Operation, Result, hashing::EntryHash, operations::Progress};
use std::{
    path::{Path, PathBuf},
    process::{Child, Command, Stdio},
    sync::mpsc,
    time::{Duration, Instant},
};
use wire::{Wire, record};
use zeroize::Zeroizing;
const OPEN: u8 = 1;
const TEXT: u8 = 2;
const CONTENT: u8 = 3;
const TEST: u8 = 4;
const HASH_ENTRIES: u8 = 5;
const HASH_FILE: u8 = 6;
const MATERIALIZE: u8 = 7;
const EXTRACT: u8 = 8;
const CREATE: u8 = 9;
const MODIFY: u8 = 10;
record!(crate::publication::Target {
    path,
    sha256,
    before
});
record!(crate::operations::ConflictDetails {
    incoming_name,
    incoming_size,
    incoming_modified,
    existing_path,
    existing_size,
    existing_modified
});
impl Wire for Zeroizing<String> {
    fn put(&self, o: &mut Vec<u8>) {
        (**self).put(o);
    }
    fn get(i: &mut &[u8]) -> Result<Self> {
        Ok(Self::new(String::get(i)?))
    }
}
record!(Progress {
    done,
    total,
    current,
    phase,
    details,
    cancelled
});
record!(EntryHash {
    name,
    sha256,
    crc32
});
struct Blob(Vec<u8>);
impl Wire for Blob {
    fn put(&self, o: &mut Vec<u8>) {
        (self.0.len() as u32).put(o);
        o.extend_from_slice(&self.0);
    }
    fn get(i: &mut &[u8]) -> Result<Self> {
        let n = u32::get(i)? as usize;
        if n > 16 << 20 || n > i.len() {
            return Err(protocol());
        }
        let (v, tail) = i.split_at(n);
        *i = tail;
        Ok(Self(v.to_vec()))
    }
}
struct Request {
    kind: u8,
    path: String,
    password: Zeroizing<String>,
    fingerprint: String,
    destination: String,
    ids: Vec<u64>,
    preset: u8,
    resource: u8,
    names: Vec<String>,
    action: u8,
    text: String,
    smart: bool,
    volume: u64,
}
record!(Request {
    kind,
    path,
    password,
    fingerprint,
    destination,
    ids,
    preset,
    resource,
    names,
    action,
    text,
    smart,
    volume
});
struct Reply {
    kind: u8,
    progress: Option<Progress>,
    archive: Option<Archive>,
    text: String,
    content: Blob,
    hashes: Vec<EntryHash>,
    error: String,
    notice_id: u64,
    targets: Vec<crate::publication::Target>,
    conflict: Option<crate::operations::ConflictDetails>,
}
record!(Reply {
    kind,
    progress,
    archive,
    text,
    content,
    hashes,
    error,
    notice_id,
    targets,
    conflict
});
impl Default for Reply {
    fn default() -> Self {
        Self {
            kind: 1,
            progress: None,
            archive: None,
            text: String::new(),
            content: Blob(vec![]),
            hashes: vec![],
            error: String::new(),
            notice_id: 0,
            targets: vec![],
            conflict: None,
        }
    }
}
fn protocol() -> ArcError {
    ArcError::new("WORKER_PROTOCOL", "Invalid worker reply")
}
fn fingerprint(a: &Archive) -> String {
    use sha2::{Digest, Sha256};
    let mut hash = Sha256::new();
    hash.update(a.format.name());
    hash.update(a.physical_size.to_le_bytes());
    for e in &a.entries {
        hash.update(&*wire::encoded(e));
    }
    hash.finalize().iter().map(|v| format!("{v:02x}")).collect()
}
impl Request {
    fn new(kind: u8, path: &Path, password: &str, op: &Operation) -> Self {
        let s = op.settings();
        Self {
            kind,
            path: path.to_string_lossy().into_owned(),
            password: Zeroizing::new(password.into()),
            fingerprint: String::new(),
            destination: String::new(),
            ids: vec![],
            preset: s.preset as u8,
            resource: s.resource as u8,
            names: vec![],
            action: 0,
            text: String::new(),
            smart: false,
            volume: 0,
        }
    }
    fn archive(kind: u8, a: &Archive, password: &str, op: &Operation) -> Self {
        let mut request = Self::new(kind, &a.path, password, op);
        request.fingerprint = fingerprint(a);
        request
    }
}
/// Explicit executable path allows integration testing without environment overrides.
pub struct Client {
    executable: PathBuf,
    timeout: Duration,
}
impl Client {
    pub fn sibling() -> Result<Self> {
        let p = std::env::current_exe()?.with_file_name(if cfg!(windows) {
            "vynxarc-worker.exe"
        } else {
            "vynxarc-worker"
        });
        Ok(Self::at(p, Duration::from_secs(30 * 60)))
    }
    pub fn at(executable: PathBuf, timeout: Duration) -> Self {
        Self {
            executable,
            timeout,
        }
    }
    pub fn open(&self, path: &Path, password: &str, op: &Operation) -> Result<Archive> {
        let a = self
            .run(Request::new(OPEN, path, password, op), op)?
            .archive
            .ok_or_else(protocol)?;
        a.validate(&crate::security::Policy::default())?;
        Ok(a)
    }
    pub fn text(&self, a: &Archive, id: u64, password: &str, op: &Operation) -> Result<String> {
        let mut r = Request::archive(TEXT, a, password, op);
        r.ids.push(id);
        Ok(self.run(r, op)?.text)
    }
    pub fn bytes(&self, a: &Archive, id: u64, password: &str, op: &Operation) -> Result<Vec<u8>> {
        let mut r = Request::archive(CONTENT, a, password, op);
        r.ids.push(id);
        Ok(self.run(r, op)?.content.0)
    }
    pub fn test(&self, a: &Archive, password: &str, op: &Operation) -> Result<()> {
        self.run(Request::archive(TEST, a, password, op), op)
            .map(|_| ())
    }
    pub fn hashes(
        &self,
        a: &Archive,
        ids: &[u64],
        password: &str,
        op: &Operation,
    ) -> Result<Vec<EntryHash>> {
        let mut r = Request::archive(HASH_ENTRIES, a, password, op);
        r.ids = ids.to_vec();
        Ok(self.run(r, op)?.hashes)
    }
    pub fn materialize(
        &self,
        a: &Archive,
        id: u64,
        destination: &Path,
        password: &str,
        op: &Operation,
    ) -> Result<String> {
        let mut r = Request::archive(MATERIALIZE, a, password, op);
        r.ids.push(id);
        r.destination = destination.to_string_lossy().into_owned();
        Ok(self.run(r, op)?.text)
    }
    pub fn extract(
        &self,
        a: &Archive,
        options: &crate::ExtractOptions,
        password: &str,
        op: &Operation,
    ) -> Result<PathBuf> {
        let mut r = Request::archive(EXTRACT, a, password, op);
        r.destination = options.destination.to_string_lossy().into_owned();
        r.ids = options.selected.clone();
        r.action = options.conflict as u8;
        r.smart = options.smart;
        Ok(self.run(r, op)?.text.into())
    }
    pub fn create(
        &self,
        options: &crate::CreateOptions,
        format: crate::Format,
        volume: u64,
        op: &Operation,
    ) -> Result<()> {
        let mut r = Request::new(CREATE, &options.output, &options.password, op);
        r.names = options
            .inputs
            .iter()
            .map(|p| p.to_string_lossy().into_owned())
            .collect();
        r.action = format as u8;
        r.volume = volume;
        self.run(r, op).map(|_| ())
    }
    pub fn modify(
        &self,
        a: &Archive,
        change: &crate::modification::Change,
        password: &str,
        op: &Operation,
    ) -> Result<String> {
        use crate::modification::Change;
        let mut r = Request::archive(MODIFY, a, password, op);
        match change {
            Change::Add { inputs, folder } => {
                r.action = 0;
                r.names = inputs
                    .iter()
                    .map(|p| p.to_string_lossy().into_owned())
                    .collect();
                r.destination = folder.clone();
            }
            Change::Delete { names } => {
                r.action = 1;
                r.names = names.clone();
            }
            Change::Rename { old, new } => {
                r.action = 2;
                r.names = vec![old.clone()];
                r.text = new.clone();
            }
            Change::Replace {
                name,
                input,
                expected_archive_hash,
                expected_input_hash,
            } => {
                r.action = 3;
                r.names = vec![
                    name.clone(),
                    input.to_string_lossy().into_owned(),
                    expected_archive_hash.clone(),
                    expected_input_hash.clone(),
                ];
            }
        }
        Ok(self.run(r, op)?.text)
    }
    pub fn hash_file(&self, path: &Path, op: &Operation) -> Result<String> {
        Ok(self.run(Request::new(HASH_FILE, path, "", op), op)?.text)
    }
    fn run(&self, request: Request, op: &Operation) -> Result<Reply> {
        op.check()?;
        let writes = matches!(request.kind, EXTRACT | CREATE | MODIFY);
        let _slot = Slot::acquire(op)?;
        let mut cmd = Command::new(&self.executable);
        cmd.stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::null());
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            cmd.creation_flags(0x08000000);
        }
        let mut child = OwnedChild(cmd.spawn().map_err(|e| {
            ArcError::new("WORKER_START", format!("Cannot start archive worker: {e}"))
        })?);
        // The worker blocks on its first frame until limits are successfully applied.
        #[cfg(windows)]
        let job = job::Job::attach(&child.0, memory_limit(request.resource))?;
        let mut input = child.0.stdin.take().ok_or_else(protocol)?;
        let mut output = child.0.stdout.take().ok_or_else(protocol)?;
        let bytes = wire::encoded(&request);
        if bytes.len() > wire::REQUEST_LIMIT {
            return Err(protocol());
        }
        drop(request);
        // Writes also run off the monitoring thread. A child stuck before reading
        // a large request cannot disable the parent's timeout/cancellation.
        let (controls, control_rx) = mpsc::sync_channel::<Vec<u8>>(8);
        let writer = std::thread::spawn(move || {
            wire::write_frame(&mut input, &bytes, wire::REQUEST_LIMIT)?;
            drop(bytes);
            while let Ok(control) = control_rx.recv() {
                wire::write_frame(&mut input, &control, 16)?;
            }
            Ok::<_, ArcError>(())
        });
        let (tx, rx) = mpsc::sync_channel(2);
        let reader = std::thread::spawn(move || {
            loop {
                let reply = wire::read_frame(&mut output, wire::RESPONSE_LIMIT)
                    .and_then(|v| wire::decoded::<Reply>(&v));
                let terminal = !matches!(&reply, Ok(r) if matches!(r.kind, 0|3|4));
                if tx.send(reply).is_err() || terminal {
                    break;
                }
            }
        });
        let start = Instant::now();
        let mut cancelled = None;
        let mut publication: Option<(u64, u64)> = None;
        let mut conflict: Option<(u64, u64)> = None;
        let result = loop {
            if let Some((remote, local)) = publication
                && let Some(accepted) = op.take_publication_reply(local)
            {
                let mut bytes = vec![if accepted { 2 } else { 3 }];
                remote.put(&mut bytes);
                let _ = controls.try_send(bytes);
                publication = None;
            }
            if let Some((remote, local)) = conflict
                && let Some((choice, all)) = op.take_conflict_reply(local)
            {
                let mut bytes = vec![4];
                remote.put(&mut bytes);
                (choice as u8).put(&mut bytes);
                all.put(&mut bytes);
                let _ = controls.try_send(bytes);
                conflict = None;
            }
            #[cfg(windows)]
            if job.memory_exceeded() {
                break Err(ArcError::new(
                    "WORKER_MEMORY",
                    "Archive worker exceeded its memory budget; operation stopped",
                ));
            }
            if op.check().is_err() && cancelled.is_none() {
                let _ = controls.try_send(vec![1]);
                cancelled = Some(Instant::now());
            }
            if cancelled.is_some_and(|t: Instant| t.elapsed() > Duration::from_secs(2)) {
                break if writes {
                    Err(ArcError::new(
                        "RESULT_UNKNOWN",
                        "Worker was forcibly cancelled; verify the destination before retrying",
                    ))
                } else {
                    op.check().and_then(|_| Err(protocol()))
                };
            }
            if start.elapsed() > self.timeout {
                break Err(ArcError::new(
                    "WORKER_TIMEOUT",
                    "Archive worker exceeded its time budget; operation stopped",
                ));
            }
            match rx.recv_timeout(Duration::from_millis(100)) {
                Ok(Ok(r)) if r.kind == 0 => {
                    if let Some(p) = r.progress {
                        op.import_progress(p);
                    } else {
                        break Err(protocol());
                    }
                }
                Ok(Ok(r)) if r.kind == 3 && r.notice_id > 0 && !r.targets.is_empty() => {
                    if publication.is_some() {
                        break Err(protocol());
                    }
                    if op
                        .publication_controlled()
                        .load(std::sync::atomic::Ordering::Relaxed)
                    {
                        match op.publish_intent(r.targets) {
                            Ok(local) => publication = Some((r.notice_id, local)),
                            Err(e) => break Err(e),
                        }
                    } else {
                        let mut bytes = vec![2];
                        r.notice_id.put(&mut bytes);
                        let _ = controls.try_send(bytes);
                    }
                }
                Ok(Ok(r)) if r.kind == 4 && r.notice_id > 0 => {
                    if conflict.is_some() {
                        break Err(protocol());
                    }
                    let Some(details) = r.conflict else {
                        break Err(protocol());
                    };
                    match op.publish_conflict(details) {
                        Ok(local) => conflict = Some((r.notice_id, local)),
                        Err(e) => break Err(e),
                    }
                }
                Ok(Ok(r)) if r.kind == 1 => break Ok(r),
                Ok(Ok(r)) if r.kind == 2 => break Err(remote_error(&r.error, &r.text)),
                Ok(Ok(_)) => break Err(protocol()),
                Ok(Err(e)) if e.code == "WORKER_PROTOCOL" => break Err(e),
                Ok(Err(_)) | Err(mpsc::RecvTimeoutError::Disconnected) => {
                    break Err(ArcError::new(
                        "WORKER_STOPPED",
                        "Archive worker stopped unexpectedly or exceeded its memory limit; other documents remain available",
                    ));
                }
                Err(mpsc::RecvTimeoutError::Timeout) => {}
            }
        };
        // Kill before joining: malformed/blocked output must not retain a child or thread.
        let _ = child.0.kill();
        let _ = child.0.wait();
        drop(rx);
        drop(controls);
        let _ = reader.join();
        let _ = writer.join();
        op.clear_remote_requests();
        result.map_err(|e| {
            if writes
                && matches!(
                    e.code,
                    "WORKER_STOPPED" | "WORKER_TIMEOUT" | "WORKER_MEMORY" | "WORKER_PROTOCOL"
                )
            {
                ArcError::new(
                    "RESULT_UNKNOWN",
                    format!(
                        "{} Verify the destination before any retry; a write may have completed.",
                        e.message
                    ),
                )
            } else {
                e
            }
        })
    }
}
struct OwnedChild(Child);
impl Drop for OwnedChild {
    fn drop(&mut self) {
        let _ = self.0.kill();
        let _ = self.0.wait();
    }
}
#[cfg(windows)]
fn memory_limit(resource: u8) -> usize {
    match resource {
        0 => 768 << 20,
        1 => 1536 << 20,
        _ => 3usize << 30,
    }
}
fn remote_error(code: &str, message: &str) -> ArcError {
    // Keep stable public error codes without leaking/interning arbitrary peer strings.
    let code = match code {
        "CANCELLED" => "CANCELLED",
        "PASSWORD" => "PASSWORD",
        "CHANGED" => "CHANGED",
        "LIMIT" => "LIMIT",
        "CORRUPT" => "CORRUPT",
        "UNSAFE_PATH" => "UNSAFE_PATH",
        "SELECTION" => "SELECTION",
        "PREVIEW" => "PREVIEW",
        "FORMAT" => "FORMAT",
        "IO" => "IO",
        "ZIP" => "ZIP",
        "7Z" => "7Z",
        "RAR" => "RAR",
        "JOURNAL" => "JOURNAL",
        "READ_ONLY" => "READ_ONLY",
        "ARCHIVE_BUSY" => "ARCHIVE_BUSY",
        "ACCESS_DENIED" => "ACCESS_DENIED",
        "REPLACE" => "REPLACE",
        "OPTIONS" => "OPTIONS",
        "CONFLICT" => "CONFLICT",
        "VERIFY" => "VERIFY",
        "CLEANUP" => "CLEANUP",
        _ => "WORKER_ERROR",
    };
    ArcError::new(code, message)
}
fn execute(r: Request, op: &Operation) -> Result<Reply> {
    if !matches!(
        r.kind,
        OPEN | TEXT
            | CONTENT
            | TEST
            | HASH_ENTRIES
            | HASH_FILE
            | MATERIALIZE
            | EXTRACT
            | CREATE
            | MODIFY
    ) {
        return Err(protocol());
    }
    op.configure(crate::settings::Settings::from_ids(r.preset, r.resource)?);
    if r.kind == CREATE {
        let options = crate::CreateOptions {
            output: r.path.into(),
            inputs: r.names.into_iter().map(PathBuf::from).collect(),
            password: r.password,
        };
        if r.volume > 0 {
            crate::operations::create_split(&options, r.volume, op)?;
        } else {
            let format = match r.action {
                0 => crate::Format::Zip,
                1 => crate::Format::SevenZ,
                2 => crate::Format::Tar,
                3 => crate::Format::TarGz,
                4 => crate::Format::Gzip,
                5 => crate::Format::Xz,
                6 => crate::Format::Bzip2,
                7 => crate::Format::Zstd,
                8 => crate::Format::Lzma,
                9 => crate::Format::TarXz,
                10 => crate::Format::TarBz2,
                11 => crate::Format::TarZst,
                _ => return Err(protocol()),
            };
            crate::operations::create_as(&options, format, op)?;
        }
        return Ok(Reply::default());
    }
    if r.kind == HASH_FILE {
        return Ok(Reply {
            text: crate::operations::hash_file(Path::new(&r.path), op)?,
            ..Reply::default()
        });
    }
    let archive = Archive::open(Path::new(&r.path), &r.password, op)?;
    if r.kind == OPEN {
        return Ok(Reply {
            archive: Some(archive),
            ..Reply::default()
        });
    }
    if fingerprint(&archive) != r.fingerprint {
        return Err(ArcError::new(
            "CHANGED",
            "Archive metadata changed; reopen this document before continuing",
        ));
    }
    let id = || r.ids.first().copied().ok_or_else(protocol);
    let mut reply = Reply::default();
    match r.kind {
        EXTRACT => {
            let conflict = match r.action {
                0 => crate::Conflict::Refuse,
                1 => crate::Conflict::Skip,
                2 => crate::Conflict::Replace,
                3 => crate::Conflict::Ask,
                4 => crate::Conflict::Rename,
                5 => crate::Conflict::Newer,
                _ => return Err(protocol()),
            };
            reply.text = crate::operations::extract(
                &archive,
                &crate::ExtractOptions {
                    destination: r.destination.into(),
                    selected: r.ids,
                    conflict,
                    smart: r.smart,
                    policy: crate::security::Policy::default(),
                },
                &r.password,
                op,
            )?
            .to_string_lossy()
            .into_owned();
        }
        MODIFY => {
            use crate::modification::Change;
            let change = match r.action {
                0 => Change::Add {
                    inputs: r.names.into_iter().map(PathBuf::from).collect(),
                    folder: r.destination,
                },
                1 => Change::Delete { names: r.names },
                2 if r.names.len() == 1 => Change::Rename {
                    old: r.names[0].clone(),
                    new: r.text,
                },
                3 if r.names.len() == 4 => Change::Replace {
                    name: r.names[0].clone(),
                    input: (&r.names[1]).into(),
                    expected_archive_hash: r.names[2].clone(),
                    expected_input_hash: r.names[3].clone(),
                },
                _ => return Err(protocol()),
            };
            reply.text =
                crate::modification::modify_with_receipt(&archive, &change, &r.password, op)?;
        }
        MATERIALIZE => {
            reply.text = crate::preview::materialize(
                &archive,
                id()?,
                Path::new(&r.destination),
                &r.password,
                op,
            )?
            .to_string_lossy()
            .into_owned()
        }
        TEXT => reply.text = crate::preview::text(&archive, id()?, &r.password, op)?,
        CONTENT => reply.content = Blob(crate::preview::bytes(&archive, id()?, &r.password, op)?),
        TEST => crate::operations::test(&archive, &r.password, op)?,
        HASH_ENTRIES => reply.hashes = crate::hashing::entries(&archive, &r.ids, &r.password, op)?,
        _ => return Err(protocol()),
    }
    Ok(reply)
}
/// Worker executable entrypoint. No filesystem request or password in arguments.
pub fn serve() -> Result<()> {
    #[cfg(windows)]
    job::suppress_error_dialogs();
    let request = {
        let frame = wire::read_frame(&mut std::io::stdin().lock(), wire::REQUEST_LIMIT)?;
        wire::decoded::<Request>(&frame)?
    };
    let op = Operation::default();
    op.control_publication(true);
    let controls = op.clone();
    std::thread::spawn(move || {
        let mut input = std::io::stdin().lock();
        loop {
            match wire::read_frame(&mut input, 16) {
                Ok(v) if *v == [1] => controls.cancel(),
                Ok(v) => {
                    let mut bytes = &v[..];
                    let result = (|| -> Result<()> {
                        let kind = u8::get(&mut bytes)?;
                        let id = u64::get(&mut bytes)?;
                        let valid = match kind {
                            2 | 3 => controls.reply_publication(id, kind == 2),
                            4 => {
                                let choice = match u8::get(&mut bytes)? {
                                    0 => crate::Conflict::Refuse,
                                    1 => crate::Conflict::Skip,
                                    2 => crate::Conflict::Replace,
                                    4 => crate::Conflict::Rename,
                                    _ => return Err(protocol()),
                                };
                                let all = bool::get(&mut bytes)?;
                                controls.reply_conflict(id, choice, all)
                            }
                            _ => return Err(protocol()),
                        };
                        if !valid || !bytes.is_empty() {
                            return Err(protocol());
                        }
                        Ok(())
                    })();
                    if result.is_err() {
                        controls.cancel();
                        break;
                    }
                }
                Err(_) => {
                    controls.cancel();
                    break;
                }
            }
        }
    });
    let work = op.clone();
    let (tx, rx) = mpsc::sync_channel(1);
    std::thread::spawn(move || {
        let result =
            std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| execute(request, &work)))
                .unwrap_or_else(|_| Err(ArcError::new("WORKER_ERROR", "Archive worker panicked")));
        let _ = tx.send(result);
    });
    let mut output = std::io::stdout().lock();
    let mut publication_sent = 0;
    let mut conflict_sent = 0;
    loop {
        let reply = match rx.recv_timeout(Duration::from_millis(100)) {
            Ok(Ok(reply)) => reply,
            Ok(Err(e)) => Reply {
                kind: 2,
                error: e.code.into(),
                text: e.message,
                ..Reply::default()
            },
            Err(mpsc::RecvTimeoutError::Timeout) => {
                if let Some((id, targets)) = op
                    .pending_publication()
                    .filter(|(id, _)| *id != publication_sent)
                {
                    publication_sent = id;
                    Reply {
                        kind: 3,
                        notice_id: id,
                        targets,
                        ..Reply::default()
                    }
                } else if let Some((id, details)) =
                    op.pending_conflict().filter(|(id, _)| *id != conflict_sent)
                {
                    conflict_sent = id;
                    Reply {
                        kind: 4,
                        notice_id: id,
                        conflict: Some(details),
                        ..Reply::default()
                    }
                } else {
                    Reply {
                        kind: 0,
                        progress: Some(op.snapshot()),
                        ..Reply::default()
                    }
                }
            }
            Err(_) => return Err(protocol()),
        };
        wire::write_frame(&mut output, &wire::encoded(&reply), wire::RESPONSE_LIMIT)?;
        if matches!(reply.kind, 1 | 2) {
            return Ok(());
        }
    }
}

static ACTIVE: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);
struct Slot;
impl Slot {
    fn acquire(op: &Operation) -> Result<Self> {
        use std::sync::atomic::Ordering;
        let start = Instant::now();
        loop {
            op.check()?;
            if ACTIVE
                .try_update(Ordering::AcqRel, Ordering::Acquire, |n| {
                    if n < 2 { Some(n + 1) } else { None }
                })
                .is_ok()
            {
                return Ok(Self);
            }
            if start.elapsed() > Duration::from_secs(120) {
                return Err(ArcError::new(
                    "WORKER_BUSY",
                    "Other archive workers are busy; retry after they finish",
                ));
            }
            std::thread::sleep(Duration::from_millis(25));
        }
    }
}
impl Drop for Slot {
    fn drop(&mut self) {
        ACTIVE.fetch_sub(1, std::sync::atomic::Ordering::Release);
    }
}
