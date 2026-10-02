#ifndef StageDir
  #error StageDir must name the validated runtime staging directory
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif
[Setup]
AppId={{957C2CE0-1072-43F7-98E5-4930AF16C309}
AppName=VYNX ARC
AppVersion=0.1.0
AppPublisher=VYNX ARC contributors
AppPublisherURL=https://github.com/Maks0101aps/VYNX-ARC
DefaultDirName={localappdata}\Programs\VYNX ARC
DefaultGroupName=VYNX ARC
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=VYNX-ARC-Setup-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\VynxArc.exe
ChangesAssociations=yes
LicenseFile=..\LICENSE
CloseApplications=yes

[Tasks]
Name: "startmenu"; Description: "Create Start menu shortcut"; Flags: checkedonce
Name: "openwith"; Description: "Add VYNX ARC to Open With (does not change defaults)"; Flags: checkedonce
Name: "desktop"; Description: "Create desktop shortcut"; Flags: unchecked
#ifdef SignedIdentity
Name: "explorer"; Description: "Enable Windows 11 Explorer commands"; Flags: unchecked
#endif

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "portable.flag,data\*,*.pdb"

[Icons]
Name: "{autoprograms}\VYNX ARC"; Filename: "{app}\VynxArc.exe"; Tasks: startmenu
Name: "{autodesktop}\VYNX ARC"; Filename: "{app}\VynxArc.exe"; Tasks: desktop

[Registry]
Root: HKCU; Subkey: "Software\Classes\VynxArc.Archive"; ValueType: string; ValueData: "VYNX ARC archive"; Flags: uninsdeletekey; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\VynxArc.Archive\DefaultIcon"; ValueType: string; ValueData: "{app}\VynxArc.exe,0"; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\VynxArc.Archive\shell\open\command"; ValueType: string; ValueData: """{app}\VynxArc.exe"" ""%1"""; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.zip\OpenWithProgids"; ValueType: none; ValueName: "VynxArc.Archive"; Flags: uninsdeletevalue; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.7z\OpenWithProgids"; ValueType: none; ValueName: "VynxArc.Archive"; Flags: uninsdeletevalue; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.tar\OpenWithProgids"; ValueType: none; ValueName: "VynxArc.Archive"; Flags: uninsdeletevalue; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.tgz\OpenWithProgids"; ValueType: none; ValueName: "VynxArc.Archive"; Flags: uninsdeletevalue; Tasks: openwith

[Run]
#ifdef SignedIdentity
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -File ""{app}\Register-Explorer.ps1"" -InstallDirectory ""{app}"""; Tasks: explorer; Flags: waituntilterminated runhidden
#endif
Filename: "{app}\VynxArc.exe"; Description: "Launch VYNX ARC"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -File ""{app}\Unregister-Explorer.ps1"""; Flags: waituntilterminated runhidden; RunOnceId: "RemoveVynxExplorerIdentity"
