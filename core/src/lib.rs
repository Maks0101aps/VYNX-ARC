pub mod archive;
pub mod compressed;
pub mod disk;
pub mod error;
pub mod ffi;
pub mod hashing;
pub mod modification;
pub mod operations;
mod priority;
#[cfg(windows)]
pub mod rar;
pub mod security;
pub mod settings;
pub mod timestamps;
pub mod volumes;

pub use archive::{Archive, Entry, Format};
pub use error::{ArcError, Result};
pub use operations::{Conflict, CreateOptions, ExtractOptions, Operation};
