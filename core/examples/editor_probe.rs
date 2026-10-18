//! Opt-in real-editor acceptance; ordinary tests never launch editor applications.
use std::{
    fs,
    path::{Path, PathBuf},
    time::Duration,
};
use vynx_arc_core::{modification::Change, worker::Client, *};
fn main() -> std::result::Result<(), Box<dyn std::error::Error>> {
    let args: Vec<_> = std::env::args().collect();
    if args.len() < 4 {
        return Err("editor_probe prepare|finish ROOT WORKER [INPUT]".into());
    }
    let phase = &args[1];
    let root = PathBuf::from(&args[2]);
    let worker = Client::at(PathBuf::from(&args[3]), Duration::from_secs(30));
    if phase == "prepare" {
        let input = PathBuf::from(args.get(4).ok_or("input required")?);
        fs::create_dir_all(&root)?;
        let root = std::path::absolute(&root)?;
        let name = input
            .file_name()
            .ok_or("filename missing")?
            .to_str()
            .ok_or("Unicode required")?;
        let mut manifest = String::new();
        for (format, ext) in [(Format::Zip, "zip"), (Format::SevenZ, "7z")] {
            for encrypted in [false, true] {
                let directory = root.join(format!(
                    "{ext}-{}",
                    if encrypted { "encrypted" } else { "plain" }
                ));
                fs::create_dir(&directory)?;
                let password = if encrypted {
                    "editor-acceptance-only"
                } else {
                    ""
                };
                let archive = directory.join(format!("source.{ext}"));
                worker.create(
                    &CreateOptions {
                        output: archive.clone(),
                        inputs: vec![input.clone()],
                        password: password.to_owned().into(),
                    },
                    format,
                    0,
                    &Operation::default(),
                )?;
                let a = worker.open(&archive, password, &Operation::default())?;
                let id = a
                    .entries
                    .iter()
                    .find(|e| !e.directory && e.name == name)
                    .ok_or("entry missing")?
                    .id;
                let copies = directory.join("working");
                fs::create_dir(&copies)?;
                let file = PathBuf::from(worker.materialize(
                    &a,
                    id,
                    &copies,
                    password,
                    &Operation::default(),
                )?);
                #[cfg(windows)]
                #[allow(clippy::permissions_set_readonly_false)]
                // Only clears the Windows read-only attribute.
                {
                    let mut p = fs::metadata(&file)?.permissions();
                    p.set_readonly(false);
                    fs::set_permissions(&file, p)?;
                }
                #[cfg(unix)]
                {
                    use std::os::unix::fs::PermissionsExt;
                    fs::set_permissions(&file, fs::Permissions::from_mode(0o600))?;
                }
                fs::write(
                    directory.join("archive.hash"),
                    worker.hash_file(&archive, &Operation::default())?,
                )?;
                fs::write(directory.join("original.bytes"), fs::read(&file)?)?;
                manifest.push_str(&format!(
                    "{}\t{}\t{}\t{}\n",
                    archive.to_string_lossy().trim_start_matches(r"\\?\"),
                    file.to_string_lossy().trim_start_matches(r"\\?\"),
                    name,
                    encrypted
                ));
            }
        }
        fs::write(root.join("cases.tsv"), manifest)?;
        println!("Prepared four editable copies with public test credentials");
    } else if phase == "finish" {
        let mut count = 0;
        for row in fs::read_to_string(root.join("cases.tsv"))?.lines() {
            let c: Vec<_> = row.split('\t').collect();
            if c.len() != 4 {
                return Err("damaged manifest".into());
            }
            let archive = Path::new(c[0]);
            let input = Path::new(c[1]);
            let directory = archive.parent().ok_or("parent missing")?;
            let password = if c[3] == "true" {
                "editor-acceptance-only"
            } else {
                ""
            };
            let archive_hash = fs::read_to_string(directory.join("archive.hash"))?;
            if worker.hash_file(archive, &Operation::default())? != archive_hash {
                return Err("source changed before save".into());
            }
            let expected = fs::read(input)?;
            if expected == fs::read(directory.join("original.bytes"))? {
                return Err("editor did not change working copy".into());
            }
            let a = worker.open(archive, password, &Operation::default())?;
            let receipt = worker.modify(
                &a,
                &Change::Replace {
                    name: c[2].into(),
                    input: input.into(),
                    expected_archive_hash: archive_hash,
                    expected_input_hash: worker.hash_file(input, &Operation::default())?,
                },
                password,
                &Operation::default(),
            )?;
            if receipt != worker.hash_file(archive, &Operation::default())? {
                return Err("publication receipt differs".into());
            }
            let a = worker.open(archive, password, &Operation::default())?;
            let id = a
                .entries
                .iter()
                .find(|e| e.name == c[2])
                .ok_or("saved entry missing")?
                .id;
            if worker.bytes(&a, id, password, &Operation::default())? != expected {
                return Err("saved content differs".into());
            }
            if fs::read(input)? != expected {
                return Err("working file changed by ARC".into());
            }
            count += 1;
        }
        println!("Verified {count} real editor saves into plain/encrypted ZIP/7Z; copies retained");
    } else {
        return Err("unknown acceptance phase".into());
    }
    Ok(())
}
