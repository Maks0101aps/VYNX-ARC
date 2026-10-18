//! Bounded, versioned local transport. No serde/debug representation of credentials.
use crate::{ArcError, Archive, Entry, Format, Result};
use std::io::{Read, Write};
use zeroize::Zeroizing;
pub const REQUEST_LIMIT: usize = 16 << 20;
pub const RESPONSE_LIMIT: usize = 128 << 20;
const MAGIC: &[u8; 8] = b"VYNXWP03";
fn invalid() -> ArcError {
    ArcError::new("WORKER_PROTOCOL", "Invalid or oversized worker message")
}
pub trait Wire: Sized {
    fn put(&self, out: &mut Vec<u8>);
    fn get(input: &mut &[u8]) -> Result<Self>;
}
fn take<'a>(input: &mut &'a [u8], n: usize) -> Result<&'a [u8]> {
    if n > input.len() {
        return Err(invalid());
    }
    let (v, tail) = input.split_at(n);
    *input = tail;
    Ok(v)
}
macro_rules! integer { ($($t:ty),*) => {$ (impl Wire for $t {
    fn put(&self, o: &mut Vec<u8>) { o.extend_from_slice(&self.to_le_bytes()); }
    fn get(i: &mut &[u8]) -> Result<Self> { Ok(Self::from_le_bytes(take(i, size_of::<Self>())?.try_into().map_err(|_|invalid())?)) }
})*}; }
integer!(u8, u32, u64);
impl Wire for bool {
    fn put(&self, o: &mut Vec<u8>) {
        u8::from(*self).put(o);
    }
    fn get(i: &mut &[u8]) -> Result<Self> {
        match u8::get(i)? {
            0 => Ok(false),
            1 => Ok(true),
            _ => Err(invalid()),
        }
    }
}
impl Wire for String {
    fn put(&self, o: &mut Vec<u8>) {
        (self.len() as u32).put(o);
        o.extend_from_slice(self.as_bytes());
    }
    fn get(i: &mut &[u8]) -> Result<Self> {
        let n = u32::get(i)? as usize;
        if n > 1 << 20 {
            return Err(invalid());
        }
        String::from_utf8(take(i, n)?.to_vec()).map_err(|_| invalid())
    }
}
impl<T: Wire> Wire for Vec<T> {
    fn put(&self, o: &mut Vec<u8>) {
        (self.len() as u32).put(o);
        for v in self {
            v.put(o);
        }
    }
    fn get(i: &mut &[u8]) -> Result<Self> {
        let n = u32::get(i)? as usize;
        // Every supported element consumes at least one input byte. Never reserve
        // from an untrusted count before checking the actual bounded frame.
        if n > 1_000_000 || n > i.len() {
            return Err(invalid());
        }
        (0..n).map(|_| T::get(i)).collect()
    }
}
impl<T: Wire> Wire for Option<T> {
    fn put(&self, o: &mut Vec<u8>) {
        self.is_some().put(o);
        if let Some(v) = self {
            v.put(o);
        }
    }
    fn get(i: &mut &[u8]) -> Result<Self> {
        if bool::get(i)? {
            Ok(Some(T::get(i)?))
        } else {
            Ok(None)
        }
    }
}
macro_rules! record { ($t:ty {$($field:ident),*}) => {impl Wire for $t {
    fn put(&self,o:&mut Vec<u8>) {$(self.$field.put(o);)*}
    fn get(i:&mut &[u8])->Result<Self> {Ok(Self{$($field: Wire::get(i)?,)*})}
}}; }
pub(crate) use record;
record!(Entry {
    id,
    name,
    size,
    packed,
    directory,
    link,
    encrypted,
    crc,
    modified,
    modified_unix
});
impl Wire for Format {
    fn put(&self, o: &mut Vec<u8>) {
        self.name().to_owned().put(o);
    }
    fn get(i: &mut &[u8]) -> Result<Self> {
        Ok(match String::get(i)?.as_str() {
            "ZIP" => Self::Zip,
            "7Z" => Self::SevenZ,
            "TAR" => Self::Tar,
            "TAR.GZ" => Self::TarGz,
            "GZIP" => Self::Gzip,
            "XZ" => Self::Xz,
            "BZIP2" => Self::Bzip2,
            "ZSTD" => Self::Zstd,
            "LZMA" => Self::Lzma,
            "TAR.XZ" => Self::TarXz,
            "TAR.BZ2" => Self::TarBz2,
            "TAR.ZST" => Self::TarZst,
            #[cfg(windows)]
            "RAR" => Self::Rar,
            _ => return Err(invalid()),
        })
    }
}
impl Wire for Archive {
    fn put(&self, o: &mut Vec<u8>) {
        self.path.to_string_lossy().into_owned().put(o);
        self.format.put(o);
        self.entries.put(o);
        self.physical_size.put(o);
    }
    fn get(i: &mut &[u8]) -> Result<Self> {
        Ok(Self {
            path: String::get(i)?.into(),
            format: Wire::get(i)?,
            entries: Wire::get(i)?,
            physical_size: Wire::get(i)?,
        })
    }
}
pub fn encoded<T: Wire>(v: &T) -> Zeroizing<Vec<u8>> {
    let mut o = Zeroizing::new(Vec::new());
    v.put(&mut o);
    o
}
pub fn decoded<T: Wire>(mut i: &[u8]) -> Result<T> {
    let v = T::get(&mut i)?;
    if !i.is_empty() {
        return Err(invalid());
    }
    Ok(v)
}
pub fn write_frame(w: &mut impl Write, bytes: &[u8], limit: usize) -> Result<()> {
    if bytes.len() > limit {
        return Err(invalid());
    }
    w.write_all(MAGIC)?;
    w.write_all(&(bytes.len() as u32).to_le_bytes())?;
    w.write_all(bytes)?;
    w.flush()?;
    Ok(())
}
pub fn read_frame(r: &mut impl Read, limit: usize) -> Result<Zeroizing<Vec<u8>>> {
    let mut header = [0; 12];
    r.read_exact(&mut header)?;
    let n = u32::from_le_bytes(header[8..].try_into().map_err(|_| invalid())?) as usize;
    if &header[..8] != MAGIC || n > limit {
        return Err(invalid());
    }
    let mut bytes = Zeroizing::new(vec![0; n]);
    r.read_exact(&mut bytes)?;
    Ok(bytes)
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rejects_wrong_version_oversize_truncation_trailing_and_counts() {
        let mut frame = Vec::new();
        write_frame(&mut frame, b"abc", 3).unwrap();
        assert_eq!(&*read_frame(&mut &frame[..], 3).unwrap(), b"abc");
        assert!(read_frame(&mut &frame[..], 2).is_err());
        frame[7] = b'9';
        assert!(read_frame(&mut &frame[..], 3).is_err());
        assert!(decoded::<Vec<String>>(&u32::MAX.to_le_bytes()).is_err());
        assert!(decoded::<bool>(&[2]).is_err());
        assert!(decoded::<u8>(&[1, 2]).is_err());
        assert!(read_frame(&mut &b"VYNXWP03"[..], 16).is_err());
    }
}
