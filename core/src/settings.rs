use crate::{ArcError, Format, Result};

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Preset {
    Store,
    Fast,
    #[default]
    Balanced,
    Maximum,
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum ResourceMode {
    Eco,
    #[default]
    Balanced,
    Maximum,
}
#[derive(Clone, Copy, Debug, Default)]
pub struct Settings {
    pub preset: Preset,
    pub resource: ResourceMode,
}
#[derive(Clone, Debug)]
pub struct Effective {
    pub codec: &'static str,
    pub level: u32,
    pub dictionary: u32,
    pub threads: u32,
    pub workers: usize,
    pub encoder_budget: u64,
}
impl Settings {
    pub fn from_ids(preset: u8, resource: u8) -> Result<Self> {
        Ok(Self {
            preset: match preset {
                0 => Preset::Store,
                1 => Preset::Fast,
                2 => Preset::Balanced,
                3 => Preset::Maximum,
                _ => return Err(ArcError::new("OPTIONS", "Unknown compression preset")),
            },
            resource: match resource {
                0 => ResourceMode::Eco,
                1 => ResourceMode::Balanced,
                2 => ResourceMode::Maximum,
                _ => return Err(ArcError::new("OPTIONS", "Unknown resource mode")),
            },
        })
    }
    pub fn effective(self, format: Format) -> Result<Effective> {
        let cpus = std::thread::available_parallelism().map_or(1, usize::from);
        let (workers, cap, dictionary_cap, budget) = match self.resource {
            ResourceMode::Eco => (1, 1, 2 << 20, 64u64 << 20),
            ResourceMode::Balanced => (cpus.min(2), 2, 8 << 20, 384u64 << 20),
            ResourceMode::Maximum => (cpus.min(8), 8, 32 << 20, 1024u64 << 20),
        };
        let level = match self.preset {
            Preset::Store => 0,
            Preset::Fast => 1,
            Preset::Balanced => 6,
            Preset::Maximum => 9,
        };
        let lzma_level = match self.preset {
            Preset::Fast => 1,
            Preset::Maximum => 9,
            _ => 5,
        };
        let dictionary = match self.preset {
            Preset::Fast => 1 << 20,
            Preset::Maximum => 32 << 20,
            _ => 8 << 20,
        }
        .min(dictionary_cap);
        let (codec, level, dictionary, threads) = match format {
            Format::Zip if self.preset == Preset::Store => ("Store", 0, 0, 1),
            Format::Zip => ("Deflate", level, 0, 1),
            Format::SevenZ if self.preset == Preset::Store => ("Copy", 0, 0, 1),
            Format::SevenZ => {
                // Conservative encoder workspace estimate; not a process RSS guarantee.
                let per_thread = dictionary as u64 * 14 + (16 << 20);
                let threads = (cpus.min(cap) as u32).min((budget / per_thread).max(1) as u32);
                ("LZMA2", lzma_level, dictionary, threads)
            }
            Format::Tar if matches!(self.preset, Preset::Store | Preset::Balanced) => {
                ("Store", 0, 0, 1)
            }
            Format::Tar => {
                return Err(ArcError::new(
                    "OPTIONS",
                    "TAR has no compression codec; choose Store",
                ));
            }
            Format::TarGz | Format::Gzip => ("Deflate", level, 0, 1),
            Format::Xz | Format::TarXz | Format::Lzma => {
                if self.preset == Preset::Store {
                    return Err(ArcError::new("OPTIONS", "This codec has no Store preset"));
                }
                (
                    if format == Format::Lzma {
                        "LZMA"
                    } else {
                        "XZ/LZMA2"
                    },
                    lzma_level,
                    dictionary,
                    if format == Format::Lzma {
                        1
                    } else {
                        (cpus.min(cap) as u32)
                            .min((budget / (dictionary as u64 * 14 + (16 << 20))).max(1) as u32)
                    },
                )
            }
            Format::Bzip2 | Format::TarBz2 => {
                if self.preset == Preset::Store {
                    return Err(ArcError::new("OPTIONS", "BZIP2 has no Store preset"));
                }
                (
                    "BZIP2",
                    if self.resource == ResourceMode::Eco {
                        level.min(3)
                    } else {
                        level
                    },
                    0,
                    1,
                )
            }
            Format::Zstd | Format::TarZst => {
                if self.preset == Preset::Store {
                    return Err(ArcError::new("OPTIONS", "ZSTD has no Store preset"));
                }
                let level = match self.preset {
                    Preset::Fast => 1,
                    Preset::Maximum => 19,
                    _ => 3,
                };
                ("ZSTD", level, dictionary_cap, cpus.min(cap) as u32)
            }
            #[cfg(windows)]
            Format::Rar => return Err(ArcError::new("READ_ONLY", "RAR creation is unsupported")),
        };
        Ok(Effective {
            codec,
            level,
            dictionary,
            threads,
            workers,
            encoder_budget: budget,
        })
    }
}
impl Effective {
    pub fn details(&self) -> String {
        format!(
            "{} level {}; dictionary/window {} MiB; codec threads {}; 7Z decoder workers {}; encoder budget {} MiB (estimate)",
            self.codec,
            self.level,
            self.dictionary >> 20,
            self.threads,
            self.workers,
            self.encoder_budget >> 20
        )
    }
}
