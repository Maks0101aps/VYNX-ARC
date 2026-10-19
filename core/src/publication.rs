//! Publication intent: verified output identity is acknowledged before commit.
use crate::{ArcError, Operation, Result, operations};
use std::path::PathBuf;
#[derive(Clone, Debug)]
pub struct Target {
    pub path: String,
    pub sha256: String,
    pub before: String,
}
#[derive(Default)]
pub(crate) struct Pending {
    pub id: u64,
    pub targets: Option<Vec<Target>>,
    pub reply: Option<bool>,
}
impl Operation {
    pub fn control_publication(&self, enabled: bool) {
        self.publication_controlled()
            .store(enabled, std::sync::atomic::Ordering::Relaxed);
    }
    pub(crate) fn prepare_publication(&self, paths: &[(PathBuf, PathBuf)]) -> Result<()> {
        if !self
            .publication_controlled()
            .load(std::sync::atomic::Ordering::Relaxed)
        {
            return self.check();
        }
        if paths.is_empty() || paths.len() > 10000 {
            return Err(ArcError::new("LIMIT", "Too many publication targets"));
        }
        let targets = paths
            .iter()
            .map(|(path, temp)| {
                Ok(Target {
                    path: std::path::absolute(path)?.to_string_lossy().into_owned(),
                    sha256: sha256(temp, self)?,
                    before: if path.exists() {
                        sha256(path, self)?
                    } else {
                        String::new()
                    },
                })
            })
            .collect::<Result<Vec<_>>>()?;
        let id = self.publish_intent(targets)?;
        loop {
            self.check()?;
            if let Some(accepted) = self.take_publication_reply(id) {
                return if accepted {
                    #[cfg(feature = "worker-diagnostics")]
                    if paths.iter().any(|(path, _)| {
                        crate::worker::diagnostics::matches(path, "before-publish")
                    }) {
                        std::process::abort();
                    }
                    Ok(())
                } else {
                    Err(ArcError::new(
                        "JOURNAL",
                        "Cannot record publication intent; archive was not published",
                    ))
                };
            }
            std::thread::sleep(std::time::Duration::from_millis(10));
        }
    }
}

/// Raw SHA-256 for durable identities; presentation reports also contain CRC32.
pub fn sha256(path: &std::path::Path, op: &Operation) -> Result<String> {
    let report = operations::hash_file(path, op)?;
    let digest = report
        .lines()
        .next()
        .and_then(|line| line.strip_prefix("SHA-256: "))
        .filter(|s| s.len() == 64 && s.bytes().all(|b| b.is_ascii_hexdigit()))
        .ok_or_else(|| ArcError::new("INTERNAL", "Invalid SHA-256 report"))?;
    Ok(digest.to_owned())
}
