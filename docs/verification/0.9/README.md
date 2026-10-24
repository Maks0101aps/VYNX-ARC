# 0.9 evidence

Candidate, not a stable release. See [acceptance](../../ACCEPTANCE_0_9.md).

Raw attempt outputs remain in `.dev/09-*`; this directory retains compact evidence.
Failed stress and initial fuzz reports are intentionally retained beside subsequent control results.
Artifact and source identities: [artifacts.json](artifacts.json).

Earlier raw stress portable hashes describe the default old ZIP; explicit-CLI tests are identified only by their CLI hash.
The final stress report correctly records no portable hash for explicit-CLI selection.
The 20k round-trip used the preceding CLI/worker before the publication-only safety correction. Final CLI decode and GUI open have separate identity-bound evidence.
