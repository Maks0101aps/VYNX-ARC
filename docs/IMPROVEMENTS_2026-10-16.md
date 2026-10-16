# Stabilization and usability improvements - 2026-10-16

Applied to the main checkout based on `a8c173b23511da4100ab46f51e6c13391037beb3`.
Changes are local and uncommitted. Version remains **0.1.0 development**;
no release, tag, push or deployment was performed.

## Implemented

- Replace yanked ZIP 8.6.0 with stable 9.0.4 and adapt fallible filename decoding.
  The [maintainer's explanation](https://github.com/zip-rs/zip2/issues/1075#issuecomment-6209381974)
  links the yanks to security issues. The source gate now fails on yanked crates.
- Preflight ZIP/ZIP64 central-directory ranges, entry records and conservative
  metadata accounting before backend parsing on open/decode/modify paths.
  Reject ambiguous EOCD records to prevent backend fallback to another unchecked
  directory. These restrictions can reject some otherwise valid ZIP variants.
- Vendor the pinned 7Z source with documented header, cumulative metadata,
  coder-chain and dictionary/arena budgets before affected allocations. Full
  license/provenance and upgrade instructions accompany the patch and package.
- Distinguish sharing/lock errors from unclassified access-denied errors. Show
  actionable GUI messages and an explicit Reload archive action. Keep one commit
  attempt and atomic verified replacement; no automatic retries or sleeps.
- Search files by name/path across the complete archive, show a Path column,
  retain exact entry IDs and restore the current folder when search is cleared.
- Add read-only UTF-8 text preview up to 1 MiB via double-click/Enter/F3, with
  stream integrity checks, cancellation and binary/oversize rejection. No
  plaintext temporary files or external handlers are used.
- Replace numeric action indexing with named actions. Move asynchronous job,
  progress and conflict orchestration to MainWindowJobs.cpp and preview to
  MainWindowPreview.cpp. Consolidate current status documentation.
- Extend CI to package and verify the deployed portable app, external locks and
  mutation smoke, retaining reports. CI itself was not run remotely here.
- Prepare a read-only-payload Windows Sandbox acceptance harness for portable,
  per-user install, Open With, same-version reinstall and uninstall checks.
  The guest script refuses to run on a host account or pre-existing installation.

## Final local validation

| Check | Result | Local evidence |
|---|---|---|
| fmt, strict Clippy, Rust tests, audit denying yanked crates | PASS; 60 Rust tests; no audit findings | `.dev/improvements-release-validated.log` |
| MSVC/CMake build and CTest | PASS; presentation, shell smoke, GUI smoke including real asynchronous text preview | same build log; 3/3 targets |
| Portable GUI/CLI without developer PATH/Qt variables | PASS | `.dev/improvements-final-portable.json` |
| ZIP/7Z/TAR/TAR.GZ round trips, independent ZIP/7Z readers, RAR4/5/multipart, split-7Z | PASS | same portable report |
| Cross-process ZIP/7Z add/delete/rename lock rejection, original SHA/MOTW preservation, cleanup, explicit retry after unlock | 6/6 PASS | `.dev/improvements-final-locks/results.json` |
| Deterministic malformed-input mutation smoke | 256/256 PASS under a 512 MiB Windows Job budget and timeouts | `.dev/improvements-final-fuzz/results.json` |
| Packaged project-path ZIP stress, with/without MOTW | 8 categories x 100 cycles PASS; 1,400 publications independently checked | `.dev/improvements-final-stress/results.json` |
| QWidget captures, light/dark Ukrainian dialogs/search/preview | 18 generated; new search/preview scenes visually inspected | `.dev/improvements-ui/` |
| Clean Windows installation/uninstallation | **NOT TESTED: environment launch failed** | `.dev/improvements-clean-windows/` |

Mutation smoke is not coverage-guided fuzzing or a security certification. Parser
budgets are not whole-process RSS/CPU limits. Remaining allocation/native-code,
filesystem-race and password-copy limitations are listed in SECURITY_MODEL.md.
See scripts/verification-requirements.txt for Python verification tools.

## Clean Windows limitation

Hyper-V enumeration returned a policy/permission denial. Windows Sandbox's
launcher briefly opened a remote-session process, but no guest report/log was
produced. A subsequent explicit CLI launch returned `0x80070103` (No more data
is available); `wsb list --raw` reported an empty environment list. The cause of
that Sandbox launch failure is not established. Computer Use inspection was
also unavailable because its native pipe could not be connected.

The prepared `.wsb`, payload and launch diagnostics are retained. No host
virtualization/security/trust settings were changed. This is a blocker for actual
clean-Windows acceptance, not evidence that the application's installer failed.
Signed modern Explorer visibility, standard-user VM behavior, upgrade from a
previous-version artifact and uninstall/reboot cleanup remain unverified.

## Artifacts

Final runnable directory: `dist/stage-c759bd2e009847ee89155a3797ac243d/`.

| Artifact | SHA-256 |
|---|---|
| VYNX-ARC-Portable-x64.zip | `e673325a47749fdcf45734bc374e88cf86fa77f2d63ca3f66b3b4ad86d102b44` |
| VYNX-ARC-Setup-x64.exe | `c22c10ccf73030fe35818da652997230e946f351640a6929a28fd945c31d3eed` |
| VYNX-ARC-Explorer-x64.msix | `c9111489a203f70dee56dfdf29620a1e0f2b2b9373e2e0f1148486273faba814` |

The pre-change artifacts and checksums are retained under
`.dev/pre-improvements-artifacts-20261016/`. Their original portable package also
passed today's baseline project-path verification and 1,400-publication stress
before source changes. Both its successful fresh evidence and the October 3
failure reports are retained. No causal resolution of the old environmental
ZIP replacement failures is claimed.

## Next release gates

Run the prepared automated subset on a functioning clean Windows Sandbox, then
complete CLEAN_VM_CHECKLIST.md on a standard-user VM with trusted signing and a
previous-version artifact. Continue coverage-guided fuzzing and remaining parser/
codec/native resource audits. Review development-branch features separately;
they were not merged as part of this change.
