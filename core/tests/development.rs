use std::{fs, path::Path};
use vynx_arc_core::{
    security::Policy,
    settings::{Preset, ResourceMode, Settings},
    *,
};
fn options(output: &Path, input: &Path) -> CreateOptions {
    CreateOptions {
        output: output.into(),
        inputs: vec![input.into()],
        password: String::new().into(),
    }
}
#[test]
fn presets_change_real_zip_codec_and_roundtrip_every_7z_setting() {
    let t = tempfile::tempdir().unwrap();
    let input = t.path().join("data");
    let data = b"actual codec settings\n".repeat(4000);
    fs::write(&input, &data).unwrap();
    for preset in [
        Preset::Store,
        Preset::Fast,
        Preset::Balanced,
        Preset::Maximum,
    ] {
        for mode in [
            ResourceMode::Eco,
            ResourceMode::Balanced,
            ResourceMode::Maximum,
        ] {
            for ext in ["zip", "7z"] {
                let output = t.path().join(format!("{preset:?}-{mode:?}.{ext}"));
                let op = Operation::default();
                op.configure(Settings {
                    preset,
                    resource: mode,
                });
                operations::create(&options(&output, &input), &op).unwrap();
                if ext == "zip" {
                    let mut z = zip::ZipArchive::new(fs::File::open(&output).unwrap()).unwrap();
                    assert_eq!(
                        z.by_index(0).unwrap().compression(),
                        if preset == Preset::Store {
                            zip::CompressionMethod::Stored
                        } else {
                            zip::CompressionMethod::Deflated
                        }
                    );
                }
                let archive = Archive::open(&output, "", &op).unwrap();
                operations::test(&archive, "", &op).unwrap();
            }
        }
    }
}
#[test]
fn standalone_and_compressed_tar_roundtrip_with_verified_bytes() {
    for ext in [
        "gz", "xz", "bz2", "zst", "lzma", "tar.xz", "tar.bz2", "tar.zst",
    ] {
        let t = tempfile::tempdir().unwrap();
        let input = t.path().join("payload");
        let data: Vec<u8> = (0..120000).map(|i| (i % 251) as u8).collect();
        fs::write(&input, &data).unwrap();
        let output = t.path().join(format!("payload.{ext}"));
        let op = Operation::default();
        operations::create(&options(&output, &input), &op).unwrap();
        let a = Archive::open(&output, "", &op).unwrap();
        operations::test(&a, "", &op).unwrap();
        let dest = t.path().join("out");
        operations::extract(
            &a,
            &ExtractOptions {
                destination: dest.clone(),
                selected: vec![],
                conflict: Conflict::Refuse,
                smart: false,
                policy: Policy::default(),
            },
            "",
            &op,
        )
        .unwrap();
        assert_eq!(fs::read(dest.join(&a.entries[0].name)).unwrap(), data);
    }
}
#[test]
fn unsupported_combinations_do_not_publish() {
    let t = tempfile::tempdir().unwrap();
    let input = t.path().join("data");
    fs::write(&input, b"bytes").unwrap();
    let output = t.path().join("bad.xz");
    let op = Operation::default();
    assert_eq!(
        operations::create_as(&options(&output, &input), Format::Zip, &op)
            .unwrap_err()
            .code,
        "FORMAT"
    );
    op.configure(Settings {
        preset: Preset::Store,
        resource: ResourceMode::Eco,
    });
    assert_eq!(
        operations::create(&options(&output, &input), &op)
            .unwrap_err()
            .code,
        "OPTIONS"
    );
    op.configure(Settings::default());
    assert_eq!(
        operations::create(&options(&output, t.path()), &op)
            .unwrap_err()
            .code,
        "INPUT"
    );
    let mut encrypted = options(&output, &input);
    encrypted.password = String::from("secret").into();
    assert_eq!(
        operations::create(&encrypted, &op).unwrap_err().code,
        "ENCRYPTION"
    );
    assert!(!output.exists());
}
#[test]
fn resource_caps_limit_encoder_workspace_and_preserve_zip_levels() {
    for mode in [
        ResourceMode::Eco,
        ResourceMode::Balanced,
        ResourceMode::Maximum,
    ] {
        for preset in [Preset::Fast, Preset::Balanced, Preset::Maximum] {
            let s = Settings {
                preset,
                resource: mode,
            };
            let e = s.effective(Format::SevenZ).unwrap();
            assert!(
                e.dictionary as u64 * 14 * e.threads as u64 + (16u64 << 20) * e.threads as u64
                    <= e.encoder_budget
            );
            assert_eq!(
                s.effective(Format::Zip).unwrap().level,
                match preset {
                    Preset::Fast => 1,
                    Preset::Maximum => 9,
                    _ => 6,
                }
            );
        }
    }
    let eco = Settings {
        preset: Preset::Maximum,
        resource: ResourceMode::Eco,
    }
    .effective(Format::SevenZ)
    .unwrap();
    assert_eq!(eco.threads, 1);
    assert_eq!(eco.workers, 1);
    assert!(eco.dictionary <= 2 << 20);
    assert!(Settings::from_ids(4, 1).is_err());
    assert!(Settings::from_ids(2, 3).is_err());
}
#[test]
fn cancelled_creation_and_decoder_stop_without_output() {
    let t = tempfile::tempdir().unwrap();
    let input = t.path().join("data");
    fs::write(&input, b"data").unwrap();
    let output = t.path().join("data.xz");
    let op = Operation::default();
    op.cancel();
    assert_eq!(
        operations::create(&options(&output, &input), &op)
            .unwrap_err()
            .code,
        "CANCELLED"
    );
    assert!(!output.exists());
    operations::create(&options(&output, &input), &Operation::default()).unwrap();
    assert!(Archive::open(&output, "", &op).is_err());
}
#[test]
fn oversized_lzma_dictionary_is_rejected_before_allocation() {
    let t = tempfile::tempdir().unwrap();
    let path = t.path().join("hostile.lzma");
    let mut bytes = vec![93];
    bytes.extend((1u32 << 30).to_le_bytes());
    bytes.extend(1u64.to_le_bytes());
    bytes.push(0);
    fs::write(&path, bytes).unwrap();
    let op = Operation::default();
    op.configure(Settings {
        preset: Preset::Balanced,
        resource: ResourceMode::Eco,
    });
    let error = Archive::open(&path, "", &op).err().unwrap();
    assert!(error.message.contains("memory"));
}

#[test]
fn zero_filled_standalone_payloads_are_not_mistaken_for_empty_tar() {
    for ext in ["gz", "xz", "bz2", "zst", "lzma"] {
        let t = tempfile::tempdir().unwrap();
        let input = t.path().join("zeros");
        fs::write(&input, vec![0; 4096]).unwrap();
        let output = t.path().join(format!("zeros.{ext}"));
        let op = Operation::default();
        operations::create(&options(&output, &input), &op).unwrap();
        let a = Archive::open(&output, "", &op).unwrap();
        assert_eq!(a.entries.len(), 1);
        assert_eq!(a.entries[0].size, 4096);
        operations::test(&a, "", &op).unwrap();
    }
}

#[test]
fn compressed_tar_trailer_corruption_is_not_hidden_by_tar_end_marker() {
    let t = tempfile::tempdir().unwrap();
    let input = t.path().join("data");
    fs::write(&input, b"trailer verification").unwrap();
    let output = t.path().join("data.tar.gz");
    let op = Operation::default();
    operations::create(&options(&output, &input), &op).unwrap();
    let mut bytes = fs::read(&output).unwrap();
    let crc_offset = bytes.len() - 8;
    bytes[crc_offset] ^= 1;
    fs::write(&output, bytes).unwrap();
    assert!(Archive::open(&output, "", &op).is_err());
}
