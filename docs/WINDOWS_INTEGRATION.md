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

Modern Explorer IExplorerCommand integration and VynxArcShell.dll are **not yet
implemented**. The installer does not offer a nonfunctional context-menu checkbox.
Integration install/uninstall must be tested on a disposable Windows profile or
clean VM before a public release. This development workstation is not a clean VM.
