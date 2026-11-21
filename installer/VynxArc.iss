#ifndef StageDir
  #error StageDir must name the validated runtime staging directory
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif
[Setup]
AppId={{957C2CE0-1072-43F7-98E5-4930AF16C309}
AppName=VYNX ARC
AppVersion=1.0.2
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
SetupIconFile=..\assets\icons\vynx-arc.ico
WizardImageFile=wizard-large.bmp
WizardSmallImageFile=wizard-small.bmp
DisableWelcomePage=no
UninstallDisplayIcon={app}\VynxArc.exe
ChangesAssociations=yes
LicenseFile=..\LICENSE
CloseApplications=yes

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "uk"; MessagesFile: "compiler:Languages\Ukrainian.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"

[Messages]
en.WelcomeLabel1=Welcome to the VYNX ARC Setup Wizard
en.WelcomeLabel2=This wizard will guide you through installing VYNX ARC 1.0.2, an offline, high-performance archive manager built with C++20, Qt 6 Widgets, and Rust.%n%nFeatures:%n• Native RAR5, 7Z, ZIP, TAR, ISO 9660/Joliet, and compressed streams%n• Supervised worker isolation with memory limits%n• Integrated one-click auto-updater%n• Completely offline with no telemetry%n%nClick Next to continue, or Cancel to exit Setup.

ru.WelcomeLabel1=Мастер установки VYNX ARC 1.0.2
ru.WelcomeLabel2=Программа установит VYNX ARC 1.0.2 — высокопроизводительный автономный архиватор на базе C++20, Qt 6 и Rust.%n%nВозможности:%n• Нативная работа с RAR5, 7Z, ZIP, TAR, ISO 9660/Joliet и потоками сжатия%n• Изоляция процессов под контролем ресурсов%n• Встроенное автообновление в один клик%n• Полная автономность и отсутствие телеметрии%n%nНажмите «Далее» для продолжения или «Отмена» для выхода.

uk.WelcomeLabel1=Майстер встановлення VYNX ARC 1.0.2
uk.WelcomeLabel2=Програма встановить VYNX ARC 1.0.2 — високопродуктивний автономний архіватор на базі C++20, Qt 6 та Rust.%n%nМожливості:%n• Нативна робота з RAR5, 7Z, ZIP, TAR, ISO 9660/Joliet та потоками стиснення%n• Ізоляція процесів під контролем ресурсів%n• Вбудоване автооновлення в один клік%n• Повна автономність і відсутність телеметрії%n%nНатисніть «Далі» для продовження або «Скасувати» для виходу.

[Tasks]
Name: "startmenu"; Description: "Create Start menu shortcut"; Flags: checkedonce
Name: "openwith"; Description: "Add VYNX ARC to Open With (does not change defaults)"; Flags: checkedonce
Name: "desktop"; Description: "Create desktop shortcut"; Flags: unchecked
Name: "iso"; Description: "Associate with .iso files (Optical Disc Images)"; Flags: unchecked
#ifdef SignedIdentity
Name: "explorer"; Description: "Enable Windows 11 Explorer commands"; Flags: unchecked
#endif

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "portable.flag,data\*,*.pdb"

[Icons]
Name: "{autoprograms}\VYNX ARC"; Filename: "{app}\VynxArc.exe"; Tasks: startmenu
Name: "{autodesktop}\VYNX ARC"; Filename: "{app}\VynxArc.exe"; Tasks: desktop

[Registry]
Root: HKCU; Subkey: "Software\Classes\VynxArc.Archive"; ValueType: string; ValueData: "VYNX ARC archive"; Flags: uninsdeletekey; Tasks: openwith or iso
Root: HKCU; Subkey: "Software\Classes\VynxArc.Archive\DefaultIcon"; ValueType: string; ValueData: "{app}\VynxArc.exe,0"; Tasks: openwith or iso
Root: HKCU; Subkey: "Software\Classes\VynxArc.Archive\shell\open\command"; ValueType: string; ValueData: """{app}\VynxArc.exe"" ""%1"""; Tasks: openwith or iso
Root: HKCU; Subkey: "Software\Classes\.zip\OpenWithProgids"; ValueType: string; ValueName: "VynxArc.Archive"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.7z\OpenWithProgids"; ValueType: string; ValueName: "VynxArc.Archive"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.tar\OpenWithProgids"; ValueType: string; ValueName: "VynxArc.Archive"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.tgz\OpenWithProgids"; ValueType: string; ValueName: "VynxArc.Archive"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.gz\OpenWithProgids"; ValueType: string; ValueName: "VynxArc.Archive"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.xz\OpenWithProgids"; ValueType: string; ValueName: "VynxArc.Archive"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.bz2\OpenWithProgids"; ValueType: string; ValueName: "VynxArc.Archive"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.zst\OpenWithProgids"; ValueType: string; ValueName: "VynxArc.Archive"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.lzma\OpenWithProgids"; ValueType: string; ValueName: "VynxArc.Archive"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.iso\OpenWithProgids"; ValueType: string; ValueName: "VynxArc.Archive"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.iso"; ValueType: string; ValueData: "VynxArc.Archive"; Flags: uninsdeletevalue; Tasks: iso
Root: HKCU; Subkey: "Software\Classes\.iso\OpenWithProgids"; ValueType: string; ValueName: "VynxArc.Archive"; ValueData: ""; Flags: uninsdeletevalue; Tasks: iso

[Run]
#ifdef SignedIdentity
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -File ""{app}\Register-Explorer.ps1"" -InstallDirectory ""{app}"""; Tasks: explorer; Flags: waituntilterminated runhidden
#endif
Filename: "{app}\VynxArc.exe"; Parameters: "--updated"; Description: "Launch VYNX ARC"; Flags: nowait postinstall; Check: ShouldLaunchAfterInstall
Filename: "{app}\VynxArc.exe"; Description: "Launch VYNX ARC"; Flags: nowait postinstall skipifsilent; Check: not ShouldLaunchAfterInstall

[Code]
function ShouldLaunchAfterInstall: Boolean;
begin
  Result := (ExpandConstant('{param:relaunch|0}') = '1');
end;

[UninstallRun]
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -File ""{app}\Unregister-Explorer.ps1"""; Flags: waituntilterminated runhidden; RunOnceId: "RemoveVynxExplorerIdentity"
