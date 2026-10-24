# Windows signing: local test and public release

The 0.9.0 development candidate has a separate, self-signed TESTONLY variant in
`dist/0.9.0-test-signed/`. It is **not publicly trusted**. The unsigned baseline in
`dist/0.9.0/` is preserved. Do not publish this variant as a trusted production release.

## Reproduce the local test

Run Windows PowerShell from the repository root, with Windows SDK
10.0.26100.0 and Inno Setup 6 installed:

```powershell
./scripts/sign-test-candidate.ps1
```

The script refuses an existing output directory. To preserve an earlier candidate
and reuse this machine's exact owned certificate:

```powershell
./scripts/sign-test-candidate.ps1 `
  -OutputDirectory ./dist/0.9.0-test-signed-new `
  -CertificateThumbprint 1E410A0C3FD8686ED161AAB761A9ED79A75CC1AA `
  -ReportPath ./dist/0.9.0-test-signed-new/test-signing.json
```

The default creates a new RSA-3072/SHA256 code-signing certificate in
`Cert:\CurrentUser\My`. Its subject is `CN=VYNX ARC`, matching the sparse MSIX
manifest. The friendly name explicitly says TESTONLY. Its CNG private key is
nonexportable and remains in the Windows user key store. No private key or PFX is
written to disk or committed. A `.cer` contains only the public certificate; it
does not confer trust. Do not import it into Root or TrustedPeople for this test.

The recorded local certificate thumbprint is
`1E410A0C3FD8686ED161AAB761A9ED79A75CC1AA`; it expires
**2026-11-23 09:10:59 UTC**. There is no timestamp. These test signatures expire
with the certificate. On another machine, create its own certificate rather than
expecting this private key to be available.

The script extracts the final portable ZIP, signs only `VynxArc.exe`,
`vynxarc-cli.exe`, `vynxarc-worker.exe`, `VynxShell.dll`, and the Explorer MSIX,
then rebuilds the installer from that signed payload and signs the outer Setup
EXE. Qt, Microsoft runtimes, and other payload files retain their original bytes.
It writes the signed Portable ZIP, Setup EXE, MSIX, and `SHA256SUMS.txt` separately.
The tampered negative-control executable is evidence only and is never executed
or included in the ZIP or installer.

The installer deliberately omits `SignedIdentity`: it does not offer or run
automatic Explorer identity registration for this untrusted certificate. The
script never installs the package or changes trusted certificate stores.

## What the evidence establishes

`docs/verification/0.9/test-signing.json` records the actual certificate, unsigned
and signed artifact hashes, embedded CMS signature checks, Windows signature
results, the tamper negative control, and smoke commands against a fresh extract
of the final signed ZIP. CMS verification uses `CheckSignature(true)` to check
cryptography independently of certificate-chain trust. Windows Authenticode
checks file integrity separately; changing a byte inside a PE section must return
`HashMismatch`.

On this Windows PowerShell 5 host, an untrusted self-signed root maps to the
`Get-AuthenticodeSignature` enum **UnknownError**, with the explicit message that
the chain terminates in an untrusted root. `X509Chain` reports `UntrustedRoot`, and
`signtool verify /pa /v` exits **1** with the same root-trust error. These are
recorded as failures of OS trust, not successful public validation. An intact
cryptographic signature does not establish a verified publisher, SmartScreen
acceptance, clean-Windows installation, or Explorer integration.

## Route to a publicly trusted release

1. The publisher must obtain an identity-validated public code-signing certificate
   with a supported hardware/managed private key, or provision an eligible
   Microsoft Artifact Signing **Public Trust** profile. The owner supplies the
   verified publisher identity and access to the signing provider. Provider
   eligibility and any purchase remain owner decisions; no account, purchase,
   credential, or paid resource was created by this work.
2. For managed signing, provision the service account, validated identity,
   certificate profile, endpoint/account/profile metadata, and an authenticated
   CI principal authorized as Certificate Profile Signer. Prefer workload identity
   federation/OIDC for CI access where supported. Keep authentication and keys out
   of repository files and logs. For hardware signing, use its installed KSP/CSP
   and exact certificate identity. Do not replace this with an exported PFX.
3. Match `installer/identity/AppxManifest.xml` Publisher exactly to the real
   certificate Subject. The current `CN=VYNX ARC` is a local test identity; a
   validated certificate may have a different full distinguished name. Review
   package-family/upgrade implications before changing it.
4. Implement a secure local/CI `VYNX_SIGN_SCRIPT` adapter for the chosen provider.
   `scripts/build-release.ps1` already calls that hook for the MSIX and final
   stage. Sign the four owned binaries and MSIX with SHA256 and a provider-approved
   RFC3161 timestamp (`/tr` plus `/td SHA256` for SignTool). Verify final signatures
   with `signtool verify /pa /v` **successfully**, including trust, digest,
   timestamp, and expected publisher, before building an Explorer-enabled
   installer. Then sign and verify the final outer Setup EXE; the existing build
   hook does **not** perform that last outer-EXE signing step automatically.
5. Generate hashes only after every final signature is in place. Repeat final
   portable, installer/upgrade, Explorer registration, and clean-Windows checks
   using the exact public candidate. Record signature chains and timestamps with
   its artifact hashes. Public signing alone is not a promise that SmartScreen
   warnings disappear.

Microsoft's current documentation was checked on 2026-10-24:

- [SignTool options and verification](https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool)
- [MSIX signing and SHA256](https://learn.microsoft.com/en-us/windows/msix/package/sign-app-package-using-signtool)
- [Certificate Subject must match MSIX Publisher](https://learn.microsoft.com/en-us/windows/msix/package/create-certificate-package-signing)
- [Artifact Signing and public trust profiles](https://learn.microsoft.com/en-us/azure/artifact-signing/overview)
- [Artifact Signing integrations and signing role](https://learn.microsoft.com/en-us/azure/artifact-signing/how-to-signing-integrations)
