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
        modified_unix: u64,
        modified_known: bool,
    }
    struct ProgressInfo {
        done: u64,
        total: u64,
        current: String,
        phase: String,
        details: String,
        cancelled: bool,
    }
    struct ConflictInfo {
        id: u64,
        incoming_name: String,
        incoming_size: u64,
        incoming_modified: u64,
        existing_path: String,
        existing_size: u64,
        existing_modified: u64,
    }
    struct CopySaveInfo {
        name: String,
        input: String,
        archive_hash: String,
        input_hash: String,
        output: String,
        format: u8,
        allow_decrypt: bool,
    }
    struct PublicationTarget {
        path: String,
        sha256: String,
        before: String,
    }
    struct EntryHashInfo {
        name: String,
        sha256: String,
        crc32: String,
    }
    extern "Rust" {
        type Archive;
        type Operation;
        fn new_operation() -> Box<Operation>;
        fn control_publication(op: &Operation, enabled: bool);
        fn publication_id(op: &Operation) -> u64;
        fn publication_targets(op: &Operation) -> Vec<PublicationTarget>;
        fn operation_input_hash(op: &Operation) -> String;
        fn reply_publication(op: &Operation, id: u64, accepted: bool) -> bool;
        fn configure_operation(op: &Operation, preset: u8, resource: u8) -> Result<()>;
        fn configure_rar_operation(
            op: &Operation,
            solid: bool,
            dictionary: u64,
            headers: bool,
            recovery: u8,
        ) -> Result<()>;
        fn compression_details(format: u8, preset: u8, resource: u8) -> Result<String>;
        fn clone_operation(op: &Operation) -> Box<Operation>;
        fn cancel(op: &Operation);
        fn progress(op: &Operation) -> ProgressInfo;
        fn conflict_request(op: &Operation) -> ConflictInfo;
        fn reply_conflict(op: &Operation, id: u64, choice: u8, all: bool) -> bool;
        fn cancelled(op: &Operation) -> bool;
        fn open_archive(path: &str, password: &str, op: &Operation) -> Result<Box<Archive>>;
        fn list_entries(archive: &Archive) -> Vec<EntryInfo>;
        fn extraction_root(archive: &Archive, destination: &str, smart: bool) -> Result<String>;
        fn archive_format(archive: &Archive) -> String;
        fn archive_writable(archive: &Archive) -> bool;
        fn preview_text(
            archive: &Archive,
            id: u64,
            password: &str,
            op: &Operation,
        ) -> Result<String>;
        fn preview_bytes(
            archive: &Archive,
            id: u64,
            password: &str,
            op: &Operation,
        ) -> Result<Vec<u8>>;
        fn materialize_entry(
            archive: &Archive,
            id: u64,
            destination: &str,
            password: &str,
            op: &Operation,
        ) -> Result<String>;
        fn convert_archive(
            archive: &Archive,
            output: &str,
            format: u8,
            source_password: &str,
            output_password: &str,
            allow_decrypt: bool,
            op: &Operation,
        ) -> Result<String>;
        fn save_copy_as(
            archive: &Archive,
            info: &CopySaveInfo,
            source_password: &str,
            output_password: &str,
            op: &Operation,
        ) -> Result<String>;
        fn replace_entry(
            archive: &Archive,
            name: &str,
            input: &str,
            archive_hash: &str,
            input_hash: &str,
            password: &str,
            op: &Operation,
        ) -> Result<String>;
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
        fn hash_entries(
            archive: &Archive,
            selected: &[u64],
            password: &str,
            op: &Operation,
        ) -> Result<Vec<EntryHashInfo>>;
        fn verify_entry_hash(
            archive: &Archive,
            id: u64,
            expected: &str,
            password: &str,
            op: &Operation,
        ) -> Result<String>;
        fn verify_file_hash(path: &str, expected: &str, op: &Operation) -> Result<String>;
        fn create_archive_as(
            output: &str,
            inputs: &[String],
            format: u8,
            password: &str,
            op: &Operation,
        ) -> Result<()>;
        fn create_split_archive(
            output: &str,
            inputs: &[String],
            volume_size: u64,
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
fn preview_text(archive: &Archive, id: u64, password: &str, op: &Operation) -> Result<String> {
    guarded(|| crate::worker::Client::sibling()?.text(archive, id, password, op))
}
fn preview_bytes(archive: &Archive, id: u64, password: &str, op: &Operation) -> Result<Vec<u8>> {
    guarded(|| crate::worker::Client::sibling()?.bytes(archive, id, password, op))
}
fn materialize_entry(
    archive: &Archive,
    id: u64,
    destination: &str,
    password: &str,
    op: &Operation,
) -> Result<String> {
    guarded(|| {
        crate::worker::Client::sibling()?.materialize(
            archive,
            id,
            Path::new(destination),
            password,
            op,
        )
    })
}
fn replace_entry(
    archive: &Archive,
    name: &str,
    input: &str,
    archive_hash: &str,
    input_hash: &str,
    password: &str,
    op: &Operation,
) -> Result<String> {
    guarded(|| {
        op.set_input_receipt(input_hash)?;
        crate::worker::Client::sibling()?.modify(
            archive,
            &crate::modification::Change::Replace {
                name: name.into(),
                input: input.into(),
                expected_archive_hash: archive_hash.into(),
                expected_input_hash: input_hash.into(),
            },
            password,
            op,
        )
    })
}

fn hash_entries(
    archive: &Archive,
    selected: &[u64],
    password: &str,
    op: &Operation,
) -> Result<Vec<bridge::EntryHashInfo>> {
    guarded(|| {
        crate::worker::Client::sibling()?
            .hashes(archive, selected, password, op)
            .map(|items| {
                items
                    .into_iter()
                    .map(|h| bridge::EntryHashInfo {
                        name: h.name,
                        sha256: h.sha256,
                        crc32: h.crc32,
                    })
                    .collect()
            })
    })
}
fn verify_entry_hash(
    archive: &Archive,
    id: u64,
    expected: &str,
    password: &str,
    op: &Operation,
) -> Result<String> {
    guarded(|| {
        crate::hashing::validate_expected(expected)?;
        let hashes = crate::worker::Client::sibling()?.hashes(archive, &[id], password, op)?;
        let h = hashes
            .first()
            .ok_or_else(|| ArcError::new("SELECTION", "Select a regular file"))?;
        crate::hashing::verification_report(&h.sha256, &h.crc32, expected)
    })
}
fn verify_file_hash(path: &str, expected: &str, op: &Operation) -> Result<String> {
    guarded(|| crate::hashing::verify_file_report(Path::new(path), expected, op))
}
fn new_operation() -> Box<Operation> {
    Box::new(Operation::default())
}
fn configure_operation(op: &Operation, preset: u8, resource: u8) -> Result<()> {
    op.configure(crate::settings::Settings::from_ids(preset, resource)?);
    Ok(())
}
fn configure_rar_operation(
    op: &Operation,
    solid: bool,
    dictionary: u64,
    headers: bool,
    recovery: u8,
) -> Result<()> {
    #[cfg(windows)]
    return op.configure_rar(crate::rar_write::Options {
        solid,
        dictionary,
        encrypt_headers: headers,
        recovery_percent: recovery,
    });
    #[cfg(not(windows))]
    {
        let _ = (op, solid, dictionary, headers, recovery);
        Err(ArcError::new("FORMAT", "RAR encoding requires Windows"))
    }
}
fn compression_details(format: u8, preset: u8, resource: u8) -> Result<String> {
    let format = match format {
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
        #[cfg(windows)]
        12 => crate::Format::Rar,
        _ => return Err(ArcError::new("FORMAT", "Unsupported format")),
    };
    Ok(crate::settings::Settings::from_ids(preset, resource)?
        .effective(format)?
        .details())
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
        phase: p.phase,
        details: p.details,
        cancelled: p.cancelled,
    }
}
fn open_archive(path: &str, password: &str, op: &Operation) -> Result<Box<Archive>> {
    guarded(|| {
        crate::worker::Client::sibling()?
            .open(Path::new(path), password, op)
            .map(Box::new)
    })
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
            modified_unix: e.modified_unix.unwrap_or(0),
            modified_known: e.modified_unix.is_some(),
        })
        .collect()
}
fn extraction_root(archive: &Archive, destination: &str, smart: bool) -> Result<String> {
    guarded(|| {
        let base = Path::new(destination);
        let root = if smart {
            let stem = archive
                .path
                .file_stem()
                .and_then(|v| v.to_str())
                .unwrap_or("Archive");
            crate::security::smart_destination(base, stem, &archive.entries)?
        } else {
            base.to_path_buf()
        };
        Ok(root.to_string_lossy().into_owned())
    })
}
fn archive_format(archive: &Archive) -> String {
    archive.format.name().into()
}
fn archive_writable(archive: &Archive) -> bool {
    archive.writable
}
fn create_split_archive(
    output: &str,
    inputs: &[String],
    volume_size: u64,
    password: &str,
    op: &Operation,
) -> Result<()> {
    guarded(|| {
        crate::worker::Client::sibling()?.create(
            &operations::CreateOptions {
                output: output.into(),
                inputs: inputs.iter().map(PathBuf::from).collect(),
                password: Zeroizing::new(password.into()),
            },
            crate::Format::for_output(Path::new(output))?,
            volume_size,
            op,
        )
    })
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
            3 => operations::Conflict::Ask,
            4 => operations::Conflict::Rename,
            5 => operations::Conflict::Newer,
            _ => return Err(ArcError::new("OPTIONS", "Invalid conflict policy")),
        };
        let options = operations::ExtractOptions {
            destination: PathBuf::from(destination),
            selected: selected.to_vec(),
            conflict,
            smart,
            policy: Policy::default(),
        };
        crate::worker::Client::sibling()?
            .extract(archive, &options, password, op)
            .map(|p| p.to_string_lossy().into_owned())
    })
}
fn test_archive(archive: &Archive, password: &str, op: &Operation) -> Result<()> {
    guarded(|| crate::worker::Client::sibling()?.test(archive, password, op))
}
fn create_archive(output: &str, inputs: &[String], password: &str, op: &Operation) -> Result<()> {
    guarded(|| {
        crate::worker::Client::sibling()?.create(
            &operations::CreateOptions {
                output: output.into(),
                inputs: inputs.iter().map(PathBuf::from).collect(),
                password: Zeroizing::new(password.into()),
            },
            crate::Format::for_output(Path::new(output))?,
            0,
            op,
        )
    })
}
fn hash_archive_file(path: &str, op: &Operation) -> Result<String> {
    guarded(|| crate::worker::Client::sibling()?.hash_file(Path::new(path), op))
}
fn cancelled(op: &Operation) -> bool {
    op.check().is_err()
}
fn conflict_request(op: &Operation) -> bridge::ConflictInfo {
    if let Some((id, request)) = op.pending_conflict() {
        bridge::ConflictInfo {
            id,
            incoming_name: request.incoming_name,
            incoming_size: request.incoming_size,
            incoming_modified: request.incoming_modified.unwrap_or(0),
            existing_path: request.existing_path,
            existing_size: request.existing_size,
            existing_modified: request.existing_modified.unwrap_or(0),
        }
    } else {
        bridge::ConflictInfo {
            id: 0,
            incoming_name: String::new(),
            incoming_size: 0,
            incoming_modified: 0,
            existing_path: String::new(),
            existing_size: 0,
            existing_modified: 0,
        }
    }
}
fn reply_conflict(op: &Operation, id: u64, choice: u8, all: bool) -> bool {
    let choice = match choice {
        0 => operations::Conflict::Refuse,
        1 => operations::Conflict::Skip,
        2 => operations::Conflict::Replace,
        4 => operations::Conflict::Rename,
        _ => return false,
    };
    op.reply_conflict(id, choice, all)
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
            4 => crate::Format::Gzip,
            5 => crate::Format::Xz,
            6 => crate::Format::Bzip2,
            7 => crate::Format::Zstd,
            8 => crate::Format::Lzma,
            9 => crate::Format::TarXz,
            10 => crate::Format::TarBz2,
            11 => crate::Format::TarZst,
            #[cfg(windows)]
            12 => crate::Format::Rar,
            _ => return Err(ArcError::new("FORMAT", "Invalid selected format")),
        };
        crate::worker::Client::sibling()?.create(
            &operations::CreateOptions {
                output: output.into(),
                inputs: inputs.iter().map(PathBuf::from).collect(),
                password: Zeroizing::new(password.into()),
            },
            format,
            0,
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
        crate::worker::Client::sibling()?
            .modify(archive, &change, password, op)
            .map(|_| ())
    })
}

fn publication_id(op: &Operation) -> u64 {
    op.pending_publication().map_or(0, |(id, _)| id)
}
fn operation_input_hash(op: &Operation) -> String {
    op.input_receipt()
}
fn publication_targets(op: &Operation) -> Vec<bridge::PublicationTarget> {
    op.pending_publication()
        .map_or_else(Vec::new, |(_, targets)| {
            targets
                .into_iter()
                .map(|t| bridge::PublicationTarget {
                    path: t.path,
                    sha256: t.sha256,
                    before: t.before,
                })
                .collect()
        })
}
fn reply_publication(op: &Operation, id: u64, accepted: bool) -> bool {
    op.reply_publication(id, accepted)
}

fn control_publication(op: &Operation, enabled: bool) {
    op.control_publication(enabled);
}

fn conversion_format(format: u8) -> Result<crate::Format> {
    match format {
        0 => Ok(crate::Format::Zip),
        1 => Ok(crate::Format::SevenZ),
        _ => Err(ArcError::new(
            "FORMAT",
            "Conversion supports ZIP and 7Z destinations",
        )),
    }
}
fn convert_archive(
    archive: &Archive,
    output: &str,
    format: u8,
    source_password: &str,
    output_password: &str,
    allow_decrypt: bool,
    op: &Operation,
) -> Result<String> {
    guarded(|| {
        crate::worker::Client::sibling()?.convert(
            archive,
            &crate::conversion::Options {
                output: output.into(),
                format: conversion_format(format)?,
                password: Zeroizing::new(output_password.into()),
                allow_decrypt,
                replacement: None,
            },
            source_password,
            op,
        )
    })
}
fn save_copy_as(
    archive: &Archive,
    info: &bridge::CopySaveInfo,
    source_password: &str,
    output_password: &str,
    op: &Operation,
) -> Result<String> {
    guarded(|| {
        crate::worker::Client::sibling()?.convert(
            archive,
            &crate::conversion::Options {
                output: (&info.output).into(),
                format: conversion_format(info.format)?,
                password: Zeroizing::new(output_password.into()),
                allow_decrypt: info.allow_decrypt,
                replacement: Some(crate::conversion::Replacement {
                    name: info.name.clone(),
                    input: (&info.input).into(),
                    archive_hash: info.archive_hash.clone(),
                    input_hash: info.input_hash.clone(),
                }),
            },
            source_password,
            op,
        )
    })
}
