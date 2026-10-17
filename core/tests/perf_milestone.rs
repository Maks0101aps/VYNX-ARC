//! Opt-in benchmark runner, also compiled unchanged against the v0.4.0 source.
use std::{fs, path::PathBuf, time::Instant};
use vynx_arc_core::{
    modification::{self, Change},
    operations, preview, *,
};
#[cfg(windows)]
fn metrics() -> (u32, usize) {
    use std::ffi::c_void;
    #[repr(C)]
    struct Counters {
        cb: u32,
        faults: u32,
        peak: usize,
        working: usize,
        peak_paged: usize,
        paged: usize,
        peak_nonpaged: usize,
        nonpaged: usize,
        pagefile: usize,
        peak_pagefile: usize,
    }
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn GetCurrentProcess() -> *mut c_void;
        fn GetProcessHandleCount(process: *mut c_void, count: *mut u32) -> i32;
    }
    #[link(name = "psapi")]
    unsafe extern "system" {
        fn GetProcessMemoryInfo(process: *mut c_void, counters: *mut Counters, size: u32) -> i32;
    }
    let mut count = 0;
    let mut counters = Counters {
        cb: std::mem::size_of::<Counters>() as u32,
        faults: 0,
        peak: 0,
        working: 0,
        peak_paged: 0,
        paged: 0,
        peak_nonpaged: 0,
        nonpaged: 0,
        pagefile: 0,
        peak_pagefile: 0,
    };
    let size = counters.cb;
    unsafe {
        GetProcessHandleCount(GetCurrentProcess(), &mut count);
        GetProcessMemoryInfo(GetCurrentProcess(), &mut counters, size);
    }
    (count, counters.working)
}
#[cfg(not(windows))]
fn metrics() -> (u32, usize) {
    (0, 0)
}
fn median(mut values: Vec<f64>) -> f64 {
    values.sort_by(f64::total_cmp);
    values[values.len() / 2]
}
#[test]
#[ignore = "Requires reproducible local benchmark corpora and output path"]
fn archive_performance_report() {
    let root = PathBuf::from(std::env::var("VYNX_BENCH_FIXTURES").unwrap());
    let name = std::env::var("VYNX_BENCH_CASE").unwrap();
    let list = fs::read_to_string(root.join("cases.tsv")).unwrap();
    let row = list
        .lines()
        .find(|row| row.split('\t').next() == Some(&name))
        .unwrap();
    let columns: Vec<_> = row.split('\t').collect();
    let password = if columns[1] == "1" {
        "benchmark-only"
    } else {
        ""
    };
    let selected = columns[2];
    let path = root.join(&name);
    let scratch = tempfile::tempdir().unwrap();
    let (mut opens, mut tests, mut previews, mut extracts, mut modifies) =
        (vec![], vec![], vec![], vec![], vec![]);
    let mut members = 0;
    for trial in 0..3 {
        let start = Instant::now();
        let archive = Archive::open(&path, password, &Operation::default()).unwrap();
        opens.push(start.elapsed().as_secs_f64());
        members = archive.entries.len();
        let id = archive
            .entries
            .iter()
            .find(|e| e.name == selected)
            .unwrap()
            .id;
        let start = Instant::now();
        operations::test(&archive, password, &Operation::default()).unwrap();
        tests.push(start.elapsed().as_secs_f64());
        let start = Instant::now();
        assert!(
            !preview::bytes(&archive, id, password, &Operation::default())
                .unwrap()
                .is_empty()
        );
        previews.push(start.elapsed().as_secs_f64());
        let output = scratch.path().join(format!("out-{trial}"));
        let start = Instant::now();
        operations::extract(
            &archive,
            &ExtractOptions {
                destination: output,
                selected: if name == "many.zip" { vec![id] } else { vec![] },
                conflict: Conflict::Refuse,
                smart: false,
                policy: security::Policy::default(),
            },
            password,
            &Operation::default(),
        )
        .unwrap();
        extracts.push(start.elapsed().as_secs_f64());
        if name.ends_with(".zip") || name.ends_with(".7z") {
            let copy = scratch.path().join(format!("copy-{trial}-{}", name));
            fs::copy(&path, &copy).unwrap();
            let target = Archive::open(&copy, password, &Operation::default()).unwrap();
            let start = Instant::now();
            modification::modify(
                &target,
                &Change::Rename {
                    old: selected.into(),
                    new: format!("{selected}.renamed"),
                },
                password,
                &Operation::default(),
            )
            .unwrap();
            modifies.push(start.elapsed().as_secs_f64());
        }
    }
    // Retention cycles use one fixed small corpus, separate from throughput datasets.
    let cycle_path = root.join("cycles.zip");
    for _ in 0..10 {
        let archive = Archive::open(&cycle_path, "", &Operation::default()).unwrap();
        let id = archive
            .entries
            .iter()
            .find(|e| e.name == "needle.txt")
            .unwrap()
            .id;
        preview::bytes(&archive, id, "", &Operation::default()).unwrap();
    }
    let before = metrics();
    let mut handles = vec![];
    let mut working = vec![];
    for _ in 0..100 {
        let archive = Archive::open(&cycle_path, "", &Operation::default()).unwrap();
        let id = archive
            .entries
            .iter()
            .find(|e| e.name == "needle.txt")
            .unwrap()
            .id;
        preview::bytes(&archive, id, "", &Operation::default()).unwrap();
        drop(archive);
        let sample = metrics();
        handles.push(sample.0);
        working.push(sample.1);
    }
    let after = metrics();
    let archive = Archive::open(&path, password, &Operation::default()).unwrap();
    let cancel = Operation::default();
    let worker_op = cancel.clone();
    let secret = password.to_owned();
    let worker = std::thread::spawn(move || operations::test(&archive, &secret, &worker_op));
    let wait = Instant::now();
    while cancel.snapshot().done == 0 && !worker.is_finished() && wait.elapsed().as_secs() < 10 {
        std::thread::yield_now();
    }
    let start = Instant::now();
    cancel.cancel();
    let interrupted = worker.join().unwrap().is_err();
    let cancel_seconds = start.elapsed().as_secs_f64();
    let json = format!(
        "{{\"case\":\"{name}\",\"members\":{members},\"extract_scope\":\"{}\",\"median_seconds\":{{\"open\":{},\"test\":{},\"preview\":{},\"extract\":{},\"modify\":{}}},\"cancelled\":{interrupted},\"cancel_seconds\":{cancel_seconds},\"handles_before\":{},\"handles_after\":{},\"working_set_before\":{},\"working_set_after\":{},\"cycle_handles\":{:?},\"cycle_working_set\":{:?}}}",
        if name == "many.zip" {
            "selected file"
        } else {
            "whole archive"
        },
        median(opens),
        median(tests),
        median(previews),
        median(extracts),
        if modifies.is_empty() {
            "null".into()
        } else {
            median(modifies).to_string()
        },
        before.0,
        after.0,
        before.1,
        after.1,
        handles,
        working
    );
    fs::write(std::env::var("VYNX_BENCH_RESULT").unwrap(), json).unwrap();
}
