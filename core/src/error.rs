use std::fmt;

#[derive(Debug)]
pub struct ArcError {
    pub code: &'static str,
    pub message: String,
}
pub type Result<T> = std::result::Result<T, ArcError>;
impl ArcError {
    pub fn new(code: &'static str, message: impl Into<String>) -> Self {
        Self {
            code,
            message: message.into(),
        }
    }
}
impl fmt::Display for ArcError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}: {}", self.code, self.message)
    }
}
impl std::error::Error for ArcError {}
impl From<std::io::Error> for ArcError {
    fn from(e: std::io::Error) -> Self {
        Self::new("IO", e.to_string())
    }
}
impl From<zip::result::ZipError> for ArcError {
    fn from(e: zip::result::ZipError) -> Self {
        Self::new("ZIP", e.to_string())
    }
}
impl From<sevenz_rust2::Error> for ArcError {
    fn from(e: sevenz_rust2::Error) -> Self {
        Self::new("7Z", e.to_string())
    }
}
