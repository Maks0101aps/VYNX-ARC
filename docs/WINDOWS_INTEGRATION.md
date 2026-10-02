# Windows integration

The executable has an asInvoker, longPathAware, PerMonitorV2 manifest. It uses the
native system window frame for minimize/maximize/close and Windows Snap behavior.
No process injection, startup entry, service, scheduled task or updater is created.

The installer is per-user and offers Start menu, desktop, and Open With registration.
Open With uses a VynxArc.Archive ProgID under HKCU and individual extension
OpenWithProgids values. It never overwrites Windows UserChoice or claims defaults.
Uninstallation removes application-owned keys and values and its shortcuts.
Existing settings are retained; an explicit keep/remove settings uninstall UI is
not yet implemented. User archives are never included in removal instructions.

The portable package contains portable.flag; settings are stored under its own
data/settings.ini. No system integration is registered when it launches.

VynxShell.dll implements IExplorerCommand and a child-command enumerator using
native COM. It links to Windows/VC runtime libraries and contains no Qt, Rust or
archive codecs. Its methods classify selected filesystem paths and launch the
GUI; decoding/compression runs in the application's background worker. The
modern menu requires app identity, supplied by VYNX-ARC-Explorer-x64.msix with
windows.comServer and windows.fileExplorerContextMenus registrations.

Archives expose Open, Extract here, Extract to named folder, Smart Extract and
Test. Other selections expose Add to archive, Add to ZIP and Add to 7Z. Mixed
archive/non-archive selections use creation commands. Open on multiple archives
shows the first and a bounded model/view chooser for the remaining selection;
it does not launch thousands of windows. Extract/Test process archives serially.
Quick ZIP/7Z commands refuse an existing output. Archive operations preserve the
same path/link/conflict checks as commands invoked inside the GUI.

The shell request protocol is binary UTF-16, limited to 10,000 paths / 8 MiB /
32,767 characters per path. Requests use unique CREATE_NEW files with an explicit
current-user/SYSTEM ACL in LocalAppData/VynxArcShell. The shell pins that directory
against renaming and rejects reparse points. The GUI accepts only bounded files
inside that inbox, opens them exclusively without following final reparse points,
and deletes consumed requests. Paths are not expanded into the Windows command
line. A crash before consumption can leave a private request file; no automatic
recursive cleanup or system-wide registry writes are performed.

Native tests cover factory/unload lifecycle, command enumeration, state for a
file/folder/archive/multiple selections, Unicode and a synthetic 1,000-item
selection sent to a separate test probe. GUI tests independently decode a
1,000-path request containing Unicode/spaces and long paths, rejecting trailing
data. These are COM/IPC tests, not proof that Windows Explorer displays the menu.

The identity package is currently **unsigned and not registered**. A trusted
signature is required. No certificate is silently imported and Developer Mode
is not changed. scripts/build-identity.ps1 creates the sparse package using the
documented /nv flag for external binary references; schema/path activation still
needs a signed registration test. VYNX_SIGN_SCRIPT may sign the package. Release
packaging only offers the installer Explorer task when signature verification
passes. Register-Explorer.ps1 refuses portable.flag; the DLL also hides commands
in portable mode. Unregister-Explorer.ps1 removes only this app's identity package
and the uninstaller runs it before deleting application files.

See [Microsoft's context-menu mechanism](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/integrate-packaged-app-with-file-explorer)
and [external-location identity/signing requirements](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/grant-identity-to-nonpackaged-apps).
Signed registration, actual menu visibility/latency, real distinct long-path
Explorer selections and install/uninstall remain clean-VM acceptance gates.
