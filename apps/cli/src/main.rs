use std::path::{Path, PathBuf};
use vynx_arc_core::{
    ArcError, Archive, Conflict, CreateOptions, ExtractOptions, Operation, Result, operations,
    security::Policy,
};

fn run() -> Result<()> {
    let mut args: Vec<String> = std::env::args().skip(1).collect();
    if args.is_empty() || args[0] == "--help" {
        println!(
            "VYNX ARC 0.1.0\nCommands:\n  list ARCHIVE\n  extract ARCHIVE --output DIRECTORY [--smart] [--skip|--replace|--keep-both]\n  create OUTPUT.zip|.7z|.tar|.tar.gz INPUT... [--preset store|fast|balanced|maximum] [--resource eco|balanced|maximum] [--split-bytes N (7Z only)]\n  add ARCHIVE INPUT...\n  delete ARCHIVE ENTRY...\n  rename ARCHIVE OLD_PATH NEW_PATH\n  test ARCHIVE\n  hash FILE\n  verify FILE DIGEST\n  hash-entry ARCHIVE ENTRY\n  verify-entry ARCHIVE ENTRY DIGEST\nStandalone: .gz .xz .bz2 .zst .lzma (one file); TAR: .tar.xz .tar.bz2 .tar.zst. Read commands also accept --resource.\nPasswords: use the GUI; passwords on command lines are deliberately unsupported.\nExit codes: 0 success, 1 operation failure, 2 usage, 3 cancelled."
        );
        return Ok(());
    }
    if args.len() < 2 {
        return Err(ArcError::new("USAGE", "Specify a file path"));
    }
    let command = args.remove(0);
    let path = args.remove(0);
    let op = Operation::default();
    let mut selected_resource = 1;
    if let Some(index) = args.iter().position(|v| v == "--resource") {
        let value = args
            .get(index + 1)
            .ok_or_else(|| ArcError::new("USAGE", "--resource needs a mode"))?;
        selected_resource = match value.as_str() {
            "eco" => 0,
            "balanced" => 1,
            "maximum" => 2,
            _ => return Err(ArcError::new("USAGE", "Unknown resource mode")),
        };
        args.drain(index..=index + 1);
        op.configure(vynx_arc_core::settings::Settings::from_ids(
            2,
            selected_resource,
        )?);
    }
    match command.as_str() {
        "hash-entry" | "verify-entry" => {
            let name = args
                .first()
                .ok_or_else(|| ArcError::new("USAGE", "Specify an entry path"))?;
            let archive = Archive::open(Path::new(&path), "", &op)?;
            let entry = archive
                .entries
                .iter()
                .find(|e| &e.name == name && !e.directory)
                .ok_or_else(|| ArcError::new("SELECTION", "Regular file not found"))?;
            let hashes = vynx_arc_core::hashing::entries(&archive, &[entry.id], "", &op)?;
            let h = &hashes[0];
            if command == "verify-entry" {
                let expected = args
                    .get(1)
                    .ok_or_else(|| ArcError::new("USAGE", "Specify SHA-256 or CRC32"))?;
                vynx_arc_core::hashing::verify(&h.sha256, &h.crc32, expected)?;
                println!("Digest matches");
            } else {
                println!("{}\nSHA-256: {}\nCRC32: {}", h.name, h.sha256, h.crc32);
            }
        }
        "verify" => {
            let expected = args
                .first()
                .ok_or_else(|| ArcError::new("USAGE", "verify FILE SHA256_OR_CRC32"))?;
            vynx_arc_core::hashing::verify_file(Path::new(&path), expected, &op)?;
            println!("Digest matches");
        }
        "add" | "delete" | "rename" => {
            use vynx_arc_core::modification::{self, Change};
            let archive = Archive::open(Path::new(&path), "", &op)?;
            let change = match command.as_str() {
                "add" => Change::Add {
                    inputs: args.into_iter().map(PathBuf::from).collect(),
                    folder: String::new(),
                },
                "delete" => Change::Delete { names: args },
                _ if args.len() == 2 => Change::Rename {
                    old: args[0].clone(),
                    new: args[1].clone(),
                },
                _ => return Err(ArcError::new("USAGE", "rename ARCHIVE OLD_PATH NEW_PATH")),
            };
            modification::modify(&archive, &change, "", &op)?;
            println!("Replacement archive verified and committed");
        }
        "hash" => println!("{}", operations::hash_file(Path::new(&path), &op)?),
        "create" => {
            let mut inputs = Vec::new();
            let mut split = None;
            let mut preset = 2;
            let mut resource = selected_resource;
            let mut iter = args.into_iter();
            while let Some(arg) = iter.next() {
                if arg == "--preset" || arg == "--resource" {
                    let value = iter
                        .next()
                        .ok_or_else(|| ArcError::new("USAGE", "Option needs a value"))?;
                    if arg == "--preset" {
                        preset = match value.as_str() {
                            "store" => 0,
                            "fast" => 1,
                            "balanced" => 2,
                            "maximum" => 3,
                            _ => return Err(ArcError::new("USAGE", "Unknown preset")),
                        };
                    } else {
                        resource = match value.as_str() {
                            "eco" => 0,
                            "balanced" => 1,
                            "maximum" => 2,
                            _ => return Err(ArcError::new("USAGE", "Unknown resource mode")),
                        };
                    }
                } else if arg == "--split-bytes" {
                    split = Some(iter.next().and_then(|v| v.parse::<u64>().ok()).ok_or_else(
                        || ArcError::new("USAGE", "--split-bytes needs a positive byte count"),
                    )?);
                } else {
                    inputs.push(PathBuf::from(arg));
                }
            }
            let options = CreateOptions {
                output: path.into(),
                inputs,
                password: String::new().into(),
            };
            op.configure(vynx_arc_core::settings::Settings::from_ids(
                preset, resource,
            )?);
            println!(
                "{}",
                op.settings()
                    .effective(vynx_arc_core::Format::for_output(&options.output)?)?
                    .details()
            );
            if let Some(size) = split {
                operations::create_split(&options, size, &op)?;
            } else {
                operations::create(&options, &op)?;
            }
        }
        "list" | "test" | "extract" => {
            let archive = Archive::open(Path::new(&path), "", &op)?;
            match command.as_str() {
                "list" => {
                    println!(
                        "{} — {} entries",
                        archive.format.name(),
                        archive.entries.len()
                    );
                    for e in archive.entries {
                        println!("{}\t{}\t{}", e.id, e.size, e.name);
                    }
                }
                "test" => {
                    operations::test(&archive, "", &op)?;
                    println!("All streams decoded successfully");
                }
                _ => {
                    let mut dest = PathBuf::from(".");
                    let mut smart = false;
                    let mut conflict = Conflict::Refuse;
                    let mut i = 0;
                    while i < args.len() {
                        match args[i].as_str() {
                            "--output" => {
                                i += 1;
                                dest = args
                                    .get(i)
                                    .ok_or_else(|| {
                                        ArcError::new("USAGE", "--output needs a directory")
                                    })?
                                    .into();
                            }
                            "--smart" => smart = true,
                            "--skip" => conflict = Conflict::Skip,
                            "--replace" => conflict = Conflict::Replace,
                            "--keep-both" => conflict = Conflict::Rename,
                            _ => return Err(ArcError::new("USAGE", "Unknown option")),
                        }
                        i += 1;
                    }
                    let root = operations::extract(
                        &archive,
                        &ExtractOptions {
                            destination: dest,
                            selected: vec![],
                            conflict,
                            smart,
                            policy: Policy::default(),
                        },
                        "",
                        &op,
                    )?;
                    println!("Extracted to {}", root.display());
                }
            }
        }
        _ => return Err(ArcError::new("USAGE", "Unknown command; use --help")),
    }
    Ok(())
}
fn main() {
    if let Err(e) = run() {
        eprintln!("{e}");
        std::process::exit(match e.code {
            "USAGE" => 2,
            "CANCELLED" => 3,
            _ => 1,
        });
    }
}
