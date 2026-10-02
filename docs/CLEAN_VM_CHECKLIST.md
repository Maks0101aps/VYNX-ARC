# Clean Windows 11 acceptance run

This is an executable checklist, not a report of a completed VM test. The current
workstation has SDKs installed; restricted PATH checks do not replace a clean VM.

1. Create a Windows 11 x64 VM and take a snapshot before installation. Use a
   normal user, no Rust/Qt/VS/archiver development tools, and leave Developer Mode
   and certificate trust unchanged. Record winver, CPU, RAM and scaling.
2. Copy dist/VYNX-ARC-Portable-x64.zip, dist/VYNX-ARC-Setup-x64.exe and
   dist/SHA256SUMS.txt. Verify SHA-256 before execution. For Explorer acceptance,
   supply a legitimately trusted signed identity package with the same publisher
   as apps/gui/app.manifest. The unsigned development package cannot pass this gate.
3. Unpack the portable ZIP in a folder with spaces and Unicode. Run VynxArc.exe
   --smoke-test and vynxarc-cli.exe --help. Open a real archive, create/extract
   ZIP/7Z including encryption, compare bytes and test wrong passwords. Verify no
   VYNX.ARC.Explorer package or VynxArc.Archive registry keys were added. Settings
   must be beside portable.flag, with no integration registered by launch.
4. Run Setup as the normal user. Test install once with optional tasks disabled,
   uninstall, then install with Open With and the signed Explorer task enabled.
   Capture installer errors instead of assuming registration succeeded. Verify
   Get-AppxPackage -Name VYNX.ARC.Explorer and HKCU OpenWithProgids. Existing default
   archive associations must remain unchanged.
5. In Explorer's Windows 11 context menu (not Show more options), check every
   command on a file, folder, archive, two archives, mixed selections, Unicode,
   paths beyond 260 characters, 500 and 1,000 distinct files. Measure menu delay
   and confirm Explorer remains responsive while the app works. Test creation
   output collisions, encrypted input, conflict apply-all and cancellation.
   No archive decoder/compressor DLL should appear in the Explorer/surrogate module
   list. VynxShell.dll and system/VC runtime modules are expected.
6. Repeat GUI checks at 100%, 125%, 150%, 200%, keyboard-only operation, Windows
   Snap and high contrast. Review labels and comparison dialogs in English,
   Ukrainian and Russian. Record actual screenshots and failures.
7. Close the application, uninstall and inspect Get-AppxPackage, app-owned HKCU
   keys/values, shortcuts and installation files. Reboot/sign in again and verify
   the menu has disappeared. User archives and unrelated associations must remain.
   Existing settings are intentionally retained by the baseline uninstaller.
8. Save logs/screenshots with the exact Git revision and artifact hashes. Restore
   the VM snapshot before a repeat run. Mark each gate PASS/FAIL/NOT TESTED; never
   convert this checklist into successful evidence without executing it.
