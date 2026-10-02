use std::path::{Path, PathBuf};
use vynx_arc_core::{
    ArcError, Archive, Conflict, CreateOptions, ExtractOptions, Operation, Result, operations,
    security::Policy,
};

fn run() -> Result<()> {
    let mut args: Vec<String> = std::env::args().skip(1).collect();
    if args.is_empty() || args[0] == "--help" {
        println!(
            "VYNX ARC 0.1.0\nCommands:\n  list ARCHIVE\n  extract ARCHIVE --output DIRECTORY [--smart] [--skip|--replace]\n  create OUTPUT.zip|.7z|.tar|.tar.gz INPUT...\n  test ARCHIVE\n  hash FILE\nPasswords: use the GUI; passwords on command lines are deliberately unsupported.\nExit codes: 0 success, 1 operation failure, 2 usage, 3 cancelled."
        );
        return Ok(());
    }
    if args.len() < 2 {
        return Err(ArcError::new("USAGE", "Specify a file path"));
    }
    let command = args.remove(0);
    let path = args.remove(0);
    let op = Operation::default();
    match command.as_str() {
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
        "create" => operations::create(
            &CreateOptions {
                output: path.into(),
                inputs: args.into_iter().map(PathBuf::from).collect(),
                password: zeroize::Zeroizing::new(String::new()),
            },
            &op,
        )?,
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
