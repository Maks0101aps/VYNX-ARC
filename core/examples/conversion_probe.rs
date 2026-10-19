//! Opt-in fixture producer for independent ZIP/7Z reader acceptance.
use std::{fs, path::PathBuf, time::Duration};
use vynx_arc_core::{conversion::Options, operations, worker::Client, *};
fn main() -> std::result::Result<(), Box<dyn std::error::Error>> {
    let args: Vec<_> = std::env::args().collect();
    if args.len() != 4 {
        return Err("conversion_probe NEW_ROOT WORKER RAR_FIXTURES".into());
    }
    let root = std::path::absolute(PathBuf::from(&args[1]))?;
    fs::create_dir(&root)?;
    let worker = Client::at(PathBuf::from(&args[2]), Duration::from_secs(60));
    let tree = root.join("tree");
    fs::create_dir_all(tree.join("empty-directory"))?;
    fs::write(tree.join("empty-file.txt"), [])?;
    fs::write(tree.join("note.txt"), "Independent reader bytes\nТекст\n")?;
    fs::write(
        tree.join("binary.bin"),
        (0..65536).map(|i| i as u8).collect::<Vec<_>>(),
    )?;
    let mut sources = Vec::new();
    for ext in ["zip", "7z", "tar", "tar.gz", "tar.xz", "tar.bz2", "tar.zst"] {
        for encrypted in [false, true] {
            if encrypted && !["zip", "7z"].contains(&ext) {
                continue;
            }
            let password = if encrypted {
                "conversion-acceptance-only"
            } else {
                ""
            };
            let path = root.join(format!("source-{}.{ext}", u8::from(encrypted)));
            operations::create(
                &CreateOptions {
                    output: path.clone(),
                    inputs: vec![tree.clone()],
                    password: password.to_owned().into(),
                },
                &Operation::default(),
            )?;
            sources.push((path, password));
        }
    }
    for name in [
        "test_read_format_rar_binary_data.rar",
        "test_read_format_rar5_compressed.rar",
    ] {
        sources.push((PathBuf::from(&args[3]).join(name), ""));
    }
    let mut manifest = String::new();
    for (index, (source, password)) in sources.iter().enumerate() {
        let op = Operation::default();
        let before = operations::hash_file(source, &op)?;
        let archive = worker.open(source, password, &op)?;
        let hashes = worker.hashes(&archive, &[], password, &op)?;
        let expected = root.join(format!("expected-{index}.tsv"));
        fs::write(
            &expected,
            hashes
                .iter()
                .map(|h| format!("{}\t{}\n", h.name.replace('\\', "/"), h.sha256))
                .collect::<String>(),
        )?;
        let directories = root.join(format!("directories-{index}.txt"));
        fs::write(
            &directories,
            archive
                .entries
                .iter()
                .filter(|e| e.directory)
                .map(|e| format!("{}\n", e.name.replace('\\', "/").trim_end_matches('/')))
                .collect::<String>(),
        )?;
        for (format, ext) in [(Format::Zip, "zip"), (Format::SevenZ, "7z")] {
            for encrypted in [false, true] {
                let output = root.join(format!("result-{index}-{}.{ext}", u8::from(encrypted)));
                worker.convert(
                    &archive,
                    &Options {
                        output: output.clone(),
                        format,
                        password: (if encrypted {
                            "conversion-acceptance-only"
                        } else {
                            ""
                        })
                        .to_owned()
                        .into(),
                        allow_decrypt: true,
                        replacement: None,
                    },
                    password,
                    &op,
                )?;
                manifest.push_str(&format!(
                    "{}\t{}\t{}\t{}\t{}\n",
                    output.display(),
                    u8::from(encrypted),
                    expected.display(),
                    directories.display(),
                    source.display()
                ));
            }
        }
        if before != operations::hash_file(source, &op)? {
            return Err("Source changed".into());
        }
    }
    fs::write(root.join("cases.tsv"), manifest)?;
    Ok(())
}
