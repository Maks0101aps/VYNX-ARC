use std::{
    fs,
    path::Path,
    time::{Duration, Instant, UNIX_EPOCH},
};
use vynx_arc_core::{operations, security::Policy, *};

fn fixture(root: &Path) -> Archive {
    let mut inputs = Vec::new();
    for (name, bytes) in [
        ("a.txt", "incoming a"),
        ("a (2).txt", "reserved name"),
        ("b.txt", "incoming b"),
    ] {
        let path = root.join(name);
        fs::write(&path, bytes).unwrap();
        inputs.push(path);
    }
    let output = root.join("archive.zip");
    operations::create(
        &CreateOptions {
            output: output.clone(),
            inputs,
            password: String::new().into(),
        },
        &Operation::default(),
    )
    .unwrap();
    Archive::open(&output, "", &Operation::default()).unwrap()
}
fn options(root: &Path, conflict: Conflict) -> ExtractOptions {
    ExtractOptions {
        destination: root.to_owned(),
        selected: vec![],
        conflict,
        smart: false,
        policy: Policy::default(),
    }
}
fn wait(op: &Operation) -> (u64, operations::ConflictDetails) {
    let deadline = Instant::now() + Duration::from_secs(10);
    loop {
        if let Some(request) = op.pending_conflict() {
            return request;
        }
        assert!(Instant::now() < deadline, "No conflict request");
        std::thread::sleep(Duration::from_millis(10));
    }
}
#[test]
fn keep_both_reserves_incoming_names() {
    let t = tempfile::tempdir().unwrap();
    let a = fixture(t.path());
    let out = t.path().join("out");
    fs::create_dir(&out).unwrap();
    fs::write(out.join("a.txt"), "existing").unwrap();
    operations::extract(
        &a,
        &options(&out, Conflict::Rename),
        "",
        &Operation::default(),
    )
    .unwrap();
    assert_eq!(fs::read(out.join("a.txt")).unwrap(), b"existing");
    assert_eq!(fs::read(out.join("a (2).txt")).unwrap(), b"reserved name");
    assert_eq!(fs::read(out.join("a (3).txt")).unwrap(), b"incoming a");
}
#[test]
fn ask_skip_all_and_reject_stale_response() {
    let t = tempfile::tempdir().unwrap();
    let a = fixture(t.path());
    let out = t.path().join("out");
    fs::create_dir(&out).unwrap();
    for name in ["a.txt", "b.txt"] {
        fs::write(out.join(name), "existing").unwrap();
    }
    let op = Operation::default();
    let actor = op.clone();
    let thread = std::thread::spawn(move || {
        let (id, details) = wait(&actor);
        assert_eq!(details.existing_size, 8);
        assert_eq!(details.incoming_size, 10);
        assert!(!actor.reply_conflict(id + 1, Conflict::Replace, false));
        assert!(actor.reply_conflict(id, Conflict::Skip, true));
    });
    operations::extract(&a, &options(&out, Conflict::Ask), "", &op).unwrap();
    thread.join().unwrap();
    for name in ["a.txt", "b.txt"] {
        assert_eq!(fs::read(out.join(name)).unwrap(), b"existing");
    }
    assert!(op.pending_conflict().is_none());
}
#[test]
fn cancel_pending_conflict_wakes_worker() {
    let t = tempfile::tempdir().unwrap();
    let a = fixture(t.path());
    let out = t.path().join("out");
    fs::create_dir(&out).unwrap();
    fs::write(out.join("a.txt"), "existing").unwrap();
    let op = Operation::default();
    let actor = op.clone();
    let thread = std::thread::spawn(move || {
        wait(&actor);
        actor.cancel();
    });
    let error = operations::extract(&a, &options(&out, Conflict::Ask), "", &op).unwrap_err();
    thread.join().unwrap();
    assert_eq!(error.code, "CANCELLED");
    assert_eq!(fs::read(out.join("a.txt")).unwrap(), b"existing");
    assert!(op.pending_conflict().is_none());
}
#[test]
fn newer_uses_metadata_and_preserves_incoming_time() {
    let t = tempfile::tempdir().unwrap();
    let mut a = fixture(t.path());
    let out = t.path().join("out");
    fs::create_dir(&out).unwrap();
    let target = out.join("a.txt");
    fs::write(&target, "existing").unwrap();
    let seconds = fs::metadata(&target)
        .unwrap()
        .modified()
        .unwrap()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_secs();
    let id = a.entries.iter().position(|e| e.name == "a.txt").unwrap();
    a.entries[id].modified_unix = Some(seconds - 60);
    operations::extract(
        &a,
        &options(&out, Conflict::Newer),
        "",
        &Operation::default(),
    )
    .unwrap();
    assert_eq!(fs::read(&target).unwrap(), b"existing");
    a.entries[id].modified_unix = Some(seconds + 60);
    operations::extract(
        &a,
        &options(&out, Conflict::Newer),
        "",
        &Operation::default(),
    )
    .unwrap();
    assert_eq!(fs::read(&target).unwrap(), b"incoming a");
    assert_eq!(
        fs::metadata(target)
            .unwrap()
            .modified()
            .unwrap()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs(),
        seconds + 60
    );
}
