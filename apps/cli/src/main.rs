use std::path::{Path, PathBuf};
use vynx_arc_core::{
    ArcError, Archive, Conflict, CreateOptions, ExtractOptions, Operation, Result, operations,
    security::Policy,
};

fn run() -> Result<()> {
    let mut args: Vec<String> = std::env::args().skip(1).collect();
    if args.is_empty() || args[0] == "--help" {
        println!(
            "VYNX ARC {VERSION}\nCommands:\n  list ARCHIVE\n  extract ARCHIVE --output DIRECTORY [--smart] [--skip|--replace|--keep-both]\n  create OUTPUT.zip|.7z|.rar|.tar|.tar.gz INPUT... [--preset store|fast|balanced|maximum] [--resource eco|balanced|maximum] [--split-bytes N (7Z or RAR)] [--solid] [--dictionary-mib N] [--encrypt-headers] [--recovery-percent 0..10]\n  add ARCHIVE INPUT...\n  delete ARCHIVE ENTRY...\n  rename ARCHIVE OLD_PATH NEW_PATH\n  test ARCHIVE\n  convert SOURCE OUTPUT.zip|.7z [--preset store|fast|balanced|maximum]\n  isolated-list|isolated-test|isolated-extract ARCHIVE (same read/extraction options)\n  hash FILE\n  verify FILE DIGEST\n  hash-entry ARCHIVE ENTRY\n  verify-entry ARCHIVE ENTRY DIGEST\nStandalone: .gz .xz .bz2 .zst .lzma (one file); TAR: .tar.xz .tar.bz2 .tar.zst. Read commands also accept --resource.\nPasswords: use the GUI or --password-stdin (one UTF-8 line, at most 4096 bytes); never put passwords on command lines.\nExit codes: 0 success, 1 operation failure, 2 usage, 3 cancelled."
        );
        return Ok(());
    }
    if args.len() < 2 {
        return Err(ArcError::new("USAGE", "Specify a file path"));
    }
    let command = args.remove(0);
    let path = args.remove(0);
    let op = Operation::default();
    let mut password = zeroize::Zeroizing::new(String::new());
    if let Some(index) = args.iter().position(|v| v == "--password-stdin") {
        args.remove(index);
        use std::io::{BufRead, Read};
        let count = std::io::stdin()
            .lock()
            .take(4097)
            .read_line(&mut password)?;
        if count > 4096 {
            return Err(ArcError::new("USAGE", "Password input exceeds 4096 bytes"));
        }
        while password.ends_with(['\n', '\r']) {
            password.pop();
        }
        if password.is_empty() {
            return Err(ArcError::new("USAGE", "Password input is empty"));
        }
    }
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
        "convert" => {
            let output = PathBuf::from(args.first().ok_or_else(|| {
                ArcError::new(
                    "USAGE",
                    "convert SOURCE OUTPUT.zip|.7z [--preset store|fast|balanced|maximum]",
                )
            })?);
            let mut preset = 2;
            let mut allow_decrypt = false;
            let mut index = 1;
            while index < args.len() {
                match args[index].as_str() {
                    "--preset" => {
                        index += 1;
                        preset = match args.get(index).map(String::as_str) {
                            Some("store") => 0,
                            Some("fast") => 1,
                            Some("balanced") => 2,
                            Some("maximum") => 3,
                            _ => return Err(ArcError::new("USAGE", "Unknown conversion preset")),
                        };
                    }
                    "--allow-decrypt" => allow_decrypt = true,
                    _ => return Err(ArcError::new("USAGE", "Unknown conversion option")),
                }
                index += 1;
            }
            op.configure(vynx_arc_core::settings::Settings::from_ids(
                preset,
                selected_resource,
            )?);
            let worker = vynx_arc_core::worker::Client::sibling()?;
            let archive = worker.open(Path::new(&path), &password, &op)?;
            let options = vynx_arc_core::conversion::Options {
                format: vynx_arc_core::Format::for_output(&output)?,
                output,
                // Source credentials never implicitly select destination encryption.
                password: String::new().into(),
                allow_decrypt,
                replacement: None,
            };
            worker.convert(&archive, &options, &password, &op)?;
            println!("Converted archive verified and published; source preserved");
        }
        "hash-entry" | "verify-entry" => {
            let name = args
                .first()
                .ok_or_else(|| ArcError::new("USAGE", "Specify an entry path"))?;
            let archive = Archive::open(Path::new(&path), &password, &op)?;
            let entry = archive
                .entries
                .iter()
                .find(|e| &e.name == name && !e.directory)
                .ok_or_else(|| ArcError::new("SELECTION", "Regular file not found"))?;
            let hashes = vynx_arc_core::hashing::entries(&archive, &[entry.id], &password, &op)?;
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
            let archive = Archive::open(Path::new(&path), &password, &op)?;
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
            if archive.format.name() == "RAR" {
                vynx_arc_core::worker::Client::sibling()?
                    .modify(&archive, &change, &password, &op)?;
            } else {
                modification::modify(&archive, &change, &password, &op)?;
            }
            println!("Replacement archive verified and committed");
        }
        "hash" => println!("{}", operations::hash_file(Path::new(&path), &op)?),
        "create" => {
            let mut inputs = Vec::new();
            let mut split = None;
            let mut preset = 2;
            let mut resource = selected_resource;
            #[cfg(windows)]
            let mut rar = vynx_arc_core::rar_write::Options::default();
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
                } else if arg == "--solid" {
                    #[cfg(windows)]
                    {
                        rar.solid = true;
                    }
                    #[cfg(not(windows))]
                    {
                        return Err(ArcError::new("USAGE", "RAR encoding requires Windows"));
                    }
                } else if arg == "--encrypt-headers" {
                    #[cfg(windows)]
                    {
                        rar.encrypt_headers = true;
                    }
                    #[cfg(not(windows))]
                    {
                        return Err(ArcError::new("USAGE", "RAR encoding requires Windows"));
                    }
                } else if arg == "--dictionary-mib" || arg == "--recovery-percent" {
                    let value =
                        iter.next()
                            .and_then(|v| v.parse::<u64>().ok())
                            .ok_or_else(|| {
                                ArcError::new("USAGE", "RAR option requires a nonnegative number")
                            })?;
                    #[cfg(windows)]
                    {
                        if arg == "--dictionary-mib" {
                            rar.dictionary = value.checked_mul(1 << 20).ok_or_else(|| {
                                ArcError::new("OPTIONS", "Dictionary size overflow")
                            })?;
                        } else {
                            rar.recovery_percent = u8::try_from(value).map_err(|_| {
                                ArcError::new("OPTIONS", "Recovery percentage overflow")
                            })?;
                        }
                    }
                    #[cfg(not(windows))]
                    {
                        let _ = value;
                        return Err(ArcError::new("USAGE", "RAR encoding requires Windows"));
                    }
                } else if arg == "--split-bytes" {
                    split = Some(
                        iter.next()
                            .and_then(|v| v.parse::<u64>().ok())
                            .filter(|size| *size > 0)
                            .ok_or_else(|| {
                                ArcError::new("USAGE", "--split-bytes needs a positive byte count")
                            })?,
                    );
                } else {
                    inputs.push(PathBuf::from(arg));
                }
            }
            let options = CreateOptions {
                output: path.into(),
                inputs,
                password: password.clone(),
            };
            op.configure(vynx_arc_core::settings::Settings::from_ids(
                preset, resource,
            )?);
            #[cfg(windows)]
            {
                if vynx_arc_core::Format::for_output(&options.output)? != vynx_arc_core::Format::Rar
                    && (rar.solid
                        || rar.encrypt_headers
                        || rar.dictionary != 0
                        || rar.recovery_percent != 0)
                {
                    return Err(ArcError::new(
                        "OPTIONS",
                        "RAR-specific options require a .rar output",
                    ));
                }
                op.configure_rar(rar)?;
            }
            println!(
                "{}",
                op.settings()
                    .effective(vynx_arc_core::Format::for_output(&options.output)?)?
                    .details()
            );
            if vynx_arc_core::Format::for_output(&options.output)?.name() == "RAR" {
                vynx_arc_core::worker::Client::sibling()?.create(
                    &options,
                    vynx_arc_core::Format::for_output(&options.output)?,
                    split.unwrap_or(0),
                    &op,
                )?;
            } else if let Some(size) = split {
                operations::create_split(&options, size, &op)?;
            } else {
                operations::create(&options, &op)?;
            }
        }
        "list" | "test" | "extract" | "isolated-list" | "isolated-test" | "isolated-extract" => {
            let isolated = command.starts_with("isolated-");
            let worker = vynx_arc_core::worker::Client::sibling()?;
            let archive = if isolated {
                worker.open(Path::new(&path), &password, &op)?
            } else {
                Archive::open(Path::new(&path), &password, &op)?
            };
            match command.as_str() {
                "list" | "isolated-list" => {
                    println!(
                        "{} — {} entries",
                        archive.format.name(),
                        archive.entries.len()
                    );
                    for e in archive.entries {
                        println!("{}\t{}\t{}", e.id, e.size, e.name);
                    }
                }
                "test" | "isolated-test" => {
                    if isolated {
                        worker.test(&archive, &password, &op)?;
                    } else {
                        operations::test(&archive, &password, &op)?;
                    }
                    println!("All streams decoded successfully");
                }
                _ => {
                    let mut dest = PathBuf::from(".");
                    let mut smart = false;
                    let mut selected = Vec::new();
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
                            "--entry" => {
                                i += 1;
                                let name = args.get(i).ok_or_else(|| {
                                    ArcError::new("USAGE", "--entry needs an archive path")
                                })?;
                                let entry = archive
                                    .entries
                                    .iter()
                                    .find(|e| e.name == *name)
                                    .ok_or_else(|| ArcError::new("SELECTION", "Entry not found"))?;
                                selected.push(entry.id);
                            }
                            "--smart" => smart = true,
                            "--skip" => conflict = Conflict::Skip,
                            "--replace" => conflict = Conflict::Replace,
                            "--keep-both" => conflict = Conflict::Rename,
                            _ => return Err(ArcError::new("USAGE", "Unknown option")),
                        }
                        i += 1;
                    }
                    let options = ExtractOptions {
                        destination: dest,
                        selected,
                        conflict,
                        smart,
                        policy: Policy::default(),
                    };
                    let root = if isolated {
                        worker.extract(&archive, &options, &password, &op)?
                    } else {
                        operations::extract(&archive, &options, &password, &op)?
                    };
                    println!("Extracted to {}", root.display());
                }
            }
        }
        _ => return Err(ArcError::new("USAGE", "Unknown command; use --help")),
    }
    Ok(())
}
const VERSION: &str = env!("CARGO_PKG_VERSION");

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
