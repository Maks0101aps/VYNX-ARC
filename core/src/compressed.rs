//! Streaming standalone codecs and compressed TAR adapters; no backend extraction.
use crate::{ArcError, Format, Operation, Result, security::Policy, settings::Effective};
use std::io::{Read, Write};

pub fn is_stream(format: Format) -> bool {
    matches!(
        format,
        Format::Gzip | Format::Xz | Format::Bzip2 | Format::Zstd | Format::Lzma
    )
}
pub fn decoder<R: Read + 'static>(
    source: R,
    format: Format,
    op: &Operation,
    compressed_bytes: u64,
) -> Result<Box<dyn Read>> {
    let memory_kb = match op.settings().resource {
        crate::settings::ResourceMode::Eco => 64 << 10,
        crate::settings::ResourceMode::Balanced => 384 << 10,
        crate::settings::ResourceMode::Maximum => 1024 << 10,
    };
    let inner: Box<dyn Read> = match format {
        Format::Tar => Box::new(source),
        Format::Gzip | Format::TarGz => Box::new(flate2::read::MultiGzDecoder::new(source)),
        Format::Xz | Format::TarXz => {
            Box::new(lzma_rust2::XzReader::new_mem_limit(source, true, memory_kb))
        }
        Format::Bzip2 | Format::TarBz2 => Box::new(bzip2::read::MultiBzDecoder::new(source)),
        Format::Zstd | Format::TarZst => {
            let mut decoder = zstd::stream::read::Decoder::new(source)?;
            decoder.window_log_max(match op.settings().resource {
                crate::settings::ResourceMode::Eco => 23,
                crate::settings::ResourceMode::Balanced => 26,
                crate::settings::ResourceMode::Maximum => 27,
            })?;
            Box::new(decoder)
        }
        Format::Lzma => Box::new(lzma_rust2::LzmaReader::new_mem_limit(
            source, memory_kb, None,
        )?),
        _ => return Err(ArcError::new("FORMAT", "Not a compressed stream")),
    };
    Ok(Box::new(LimitedReader {
        inner,
        op: op.clone(),
        bytes: 0,
        limit: Policy::default().max_total_bytes.min(
            compressed_bytes
                .saturating_mul(Policy::default().max_ratio)
                .max(1 << 20),
        ),
    }))
}
struct LimitedReader {
    inner: Box<dyn Read>,
    op: Operation,
    bytes: u64,
    limit: u64,
}
impl Read for LimitedReader {
    fn read(&mut self, buffer: &mut [u8]) -> std::io::Result<usize> {
        self.op.check().map_err(std::io::Error::other)?;
        let n = self.inner.read(buffer)?;
        self.bytes = self
            .bytes
            .checked_add(n as u64)
            .ok_or_else(|| std::io::Error::other("Decoded size overflow"))?;
        if self.bytes > self.limit {
            return Err(std::io::Error::other("Decoded stream exceeds safety limit"));
        }
        Ok(n)
    }
}
pub struct CrcReader<'a> {
    inner: &'a mut dyn Read,
    crc: crc32fast::Hasher,
}
impl<'a> CrcReader<'a> {
    pub fn new(inner: &'a mut dyn Read) -> Self {
        Self {
            inner,
            crc: crc32fast::Hasher::new(),
        }
    }
    pub fn crc(&self) -> u32 {
        self.crc.clone().finalize()
    }
}
impl Read for CrcReader<'_> {
    fn read(&mut self, buffer: &mut [u8]) -> std::io::Result<usize> {
        let n = self.inner.read(buffer)?;
        self.crc.update(&buffer[..n]);
        Ok(n)
    }
}
pub fn encode(
    target: &mut std::fs::File,
    format: Format,
    config: &Effective,
    size: Option<u64>,
    mut write: impl FnMut(&mut dyn Write) -> Result<()>,
) -> Result<()> {
    match format {
        Format::Gzip | Format::TarGz => {
            let mut writer =
                flate2::write::GzEncoder::new(target, flate2::Compression::new(config.level));
            write(&mut writer)?;
            writer.finish()?;
        }
        Format::Bzip2 | Format::TarBz2 => {
            let mut writer =
                bzip2::write::BzEncoder::new(target, bzip2::Compression::new(config.level));
            write(&mut writer)?;
            writer.finish()?;
        }
        Format::Xz | Format::TarXz => {
            let mut options = lzma_rust2::XzOptions::with_preset(config.level);
            options.lzma_options.dict_size = config.dictionary;
            options.block_size = std::num::NonZeroU64::new(config.dictionary as u64 * 2);
            if config.threads > 1 {
                let mut writer = lzma_rust2::XzWriterMt::new(target, options, config.threads)?;
                write(&mut writer)?;
                writer.finish()?;
            } else {
                let mut writer = lzma_rust2::XzWriter::new(target, options)?;
                write(&mut writer)?;
                writer.finish()?;
            }
        }
        Format::Zstd | Format::TarZst => {
            let mut writer = zstd::stream::write::Encoder::new(target, config.level as i32)?;
            writer.multithread(if config.threads == 1 {
                0
            } else {
                config.threads
            })?;
            writer.window_log(config.dictionary.ilog2())?;
            write(&mut writer)?;
            writer.finish()?;
        }
        Format::Lzma => {
            let mut options = lzma_rust2::LzmaOptions::with_preset(config.level);
            options.dict_size = config.dictionary;
            let mut writer = lzma_rust2::LzmaWriter::new_use_header(target, &options, size)?;
            write(&mut writer)?;
            writer.finish()?;
        }
        _ => return Err(ArcError::new("FORMAT", "Not a writable compressed stream")),
    }
    Ok(())
}
