use crate::{ArcError, Archive, Operation, Result, operations, security::Policy};
use std::{
    panic::{AssertUnwindSafe, catch_unwind},
    path::{Path, PathBuf},
};
use zeroize::Zeroizing;

#[cxx::bridge(namespace = "vynx")]
pub mod bridge {
    struct EntryInfo {
        id: u64,
        name: String,
        size: u64,
        packed: u64,
        directory: bool,
        encrypted: bool,
        crc: String,
        modified: String,
    }
    struct ProgressInfo {
        done: u64,
        total: u64,
        current: String,
    }
    extern "Rust" {
        type Archive;
        type Operation;
        fn new_operation() -> Box<Operation>;
        fn clone_operation(op: &Operation) -> Box<Operation>;
        fn cancel(op: &Operation);
        fn progress(op: &Operation) -> ProgressInfo;
        fn open_archive(path: &str, password: &str, op: &Operation) -> Result<Box<Archive>>;
        fn list_entries(archive: &Archive) -> Vec<EntryInfo>;
        fn archive_format(archive: &Archive) -> String;
        fn extract_archive(
            archive: &Archive,
            destination: &str,
            selected: &[u64],
            conflict: u8,
            smart: bool,
            password: &str,
            op: &Operation,
        ) -> Result<String>;
        fn test_archive(archive: &Archive, password: &str, op: &Operation) -> Result<()>;
        fn create_archive(
            output: &str,
            inputs: &[String],
            password: &str,
            op: &Operation,
        ) -> Result<()>;
        fn hash_archive_file(path: &str, op: &Operation) -> Result<String>;
        fn create_archive_as(
            output: &str,
            inputs: &[String],
            format: u8,
            password: &str,
            op: &Operation,
        ) -> Result<()>;
        fn modify_archive(
            archive: &Archive,
            kind: u8,
            names: &[String],
            new_name: &str,
            folder: &str,
            password: &str,
            op: &Operation,
        ) -> Result<()>;
    }
}
fn guarded<T>(f: impl FnOnce() -> Result<T>) -> Result<T> {
    catch_unwind(AssertUnwindSafe(f)).unwrap_or_else(|_| {
        Err(ArcError::new(
            "INTERNAL",
            "Unexpected core failure; operation stopped",
        ))
    })
}
fn new_operation() -> Box<Operation> {
    Box::new(Operation::default())
}
fn clone_operation(op: &Operation) -> Box<Operation> {
    Box::new(op.clone())
}
fn cancel(op: &Operation) {
    op.cancel();
}
fn progress(op: &Operation) -> bridge::ProgressInfo {
    let p = op.snapshot();
    bridge::ProgressInfo {
        done: p.done,
        total: p.total,
        current: p.current,
    }
}
fn open_archive(path: &str, password: &str, op: &Operation) -> Result<Box<Archive>> {
    guarded(|| Archive::open(Path::new(path), password, op).map(Box::new))
}
fn list_entries(archive: &Archive) -> Vec<bridge::EntryInfo> {
    archive
        .entries
        .iter()
        .map(|e| bridge::EntryInfo {
            id: e.id,
            name: e.name.clone(),
            size: e.size,
            packed: e.packed,
            directory: e.directory,
            encrypted: e.encrypted,
            crc: e.crc.map(|v| format!("{v:08x}")).unwrap_or_default(),
            modified: e.modified.clone(),
        })
        .collect()
}
fn archive_format(archive: &Archive) -> String {
    archive.format.name().into()
}
fn extract_archive(
    archive: &Archive,
    destination: &str,
    selected: &[u64],
    conflict: u8,
    smart: bool,
    password: &str,
    op: &Operation,
) -> Result<String> {
    guarded(|| {
        let conflict = match conflict {
            0 => operations::Conflict::Refuse,
            1 => operations::Conflict::Skip,
            2 => operations::Conflict::Replace,
            _ => return Err(ArcError::new("OPTIONS", "Invalid conflict policy")),
        };
        let options = operations::ExtractOptions {
            destination: PathBuf::from(destination),
            selected: selected.to_vec(),
            conflict,
            smart,
            policy: Policy::default(),
        };
        operations::extract(archive, &options, password, op)
            .map(|p| p.to_string_lossy().into_owned())
    })
}
fn test_archive(archive: &Archive, password: &str, op: &Operation) -> Result<()> {
    guarded(|| operations::test(archive, password, op))
}
fn create_archive(output: &str, inputs: &[String], password: &str, op: &Operation) -> Result<()> {
    guarded(|| {
        operations::create(
            &operations::CreateOptions {
                output: output.into(),
                inputs: inputs.iter().map(PathBuf::from).collect(),
                password: Zeroizing::new(password.into()),
            },
            op,
        )
    })
}
fn hash_archive_file(path: &str, op: &Operation) -> Result<String> {
    guarded(|| operations::hash_file(Path::new(path), op))
}
fn create_archive_as(
    output: &str,
    inputs: &[String],
    format: u8,
    password: &str,
    op: &Operation,
) -> Result<()> {
    guarded(|| {
        let format = match format {
            0 => crate::Format::Zip,
            1 => crate::Format::SevenZ,
            2 => crate::Format::Tar,
            3 => crate::Format::TarGz,
            _ => return Err(ArcError::new("FORMAT", "Invalid selected format")),
        };
        operations::create_as(
            &operations::CreateOptions {
                output: output.into(),
                inputs: inputs.iter().map(PathBuf::from).collect(),
                password: Zeroizing::new(password.into()),
            },
            format,
            op,
        )
    })
}
fn modify_archive(
    archive: &Archive,
    kind: u8,
    names: &[String],
    new_name: &str,
    folder: &str,
    password: &str,
    op: &Operation,
) -> Result<()> {
    guarded(|| {
        use crate::modification::Change;
        let change = match kind {
            0 => Change::Add {
                inputs: names.iter().map(PathBuf::from).collect(),
                folder: folder.into(),
            },
            1 => Change::Delete {
                names: names.to_vec(),
            },
            2 if names.len() == 1 => Change::Rename {
                old: names[0].clone(),
                new: new_name.into(),
            },
            _ => return Err(ArcError::new("OPTIONS", "Invalid modification request")),
        };
        crate::modification::modify(archive, &change, password, op)
    })
}
