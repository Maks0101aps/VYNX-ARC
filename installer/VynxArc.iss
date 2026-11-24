#ifndef StageDir
  #error StageDir must name the validated runtime staging directory
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif
#define MyAppVersion "1.1.1"
[Setup]
AppId={{957C2CE0-1072-43F7-98E5-4930AF16C309}
AppName=VYNX ARC
AppVersion={#MyAppVersion}
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
en.WelcomeLabel2=This wizard will guide you through installing VYNX ARC {#MyAppVersion}, an offline, high-performance archive manager built with C++20, Qt 6 Widgets, and Rust.%n%nFeatures:%n• Modern WinRAR-inspired interface with high-DPI SVG vector assets, rubberband selection, and refined Light/Dark themes%n• Native RAR5, 7Z, ZIP, TAR, ISO 9660/Joliet, WIM/ESD/SWM, multi-volume archives, and compressed streams%n• Safe staged modification for multi-volume (.7z, .rar) archives%n• Supervised worker isolation with memory limits%n• Completely offline with no telemetry%n%nClick Next to continue, or Cancel to exit Setup.

ru.WelcomeLabel1=Мастер установки VYNX ARC 1.1.0
ru.WelcomeLabel2=Программа установит VYNX ARC {#MyAppVersion} — высокопроизводительный автономный архиватор на базе C++20, Qt 6 и Rust.%n%nВозможности:%n• Новый современный неоклассический интерфейс в стиле WinRAR с векторными SVG-иконками, рамкой выделения и светлой/тёмной темами%n• Нативная работа с RAR5, 7Z, ZIP, TAR, ISO 9660/Joliet, WIM/ESD/SWM, многотомными архивами и потоками сжатия%n• Безопасное транзакционное редактирование многотомных архивов%n• Изоляция процессов под контролем ресурсов%n• Полная автономность и отсутствие телеметрии%n%nНажмите «Далее» для продолжения или «Отмена» для выхода.

uk.WelcomeLabel1=Майстер встановлення VYNX ARC 1.1.0
uk.WelcomeLabel2=Програма встановить VYNX ARC {#MyAppVersion} — високопродуктивний автономний архіватор на базі C++20, Qt 6 та Rust.%n%nМожливості:%n• Новий сучасний неокласичний інтерфейс у стилі WinRAR з векторними SVG-іконками, рамкою виділення та світлою/темною темами%n• Нативна робота з RAR5, 7Z, ZIP, TAR, ISO 9660/Joliet, WIM/ESD/SWM, багатотомними архівами та потоками стиснення%n• Безпечне транзакційне редагування багатотомних архівів%n• Ізоляція процесів під контролем ресурсів%n• Повна автономність і відсутність телеметрії%n%nНатисніть «Далі» для продовження або «Скасувати» для виходу.

[Tasks]
Name: "startmenu"; Description: "Create Start menu shortcut"; Flags: checkedonce
Name: "openwith"; Description: "Add VYNX ARC to Open With (does not change defaults)"; Flags: checkedonce
Name: "desktop"; Description: "Create desktop shortcut"; Flags: unchecked
Name: "iso"; Description: "Associate with .iso files (Optical Disc Images)"; Flags: unchecked
Name: "wim"; Description: "Associate with .wim files (Windows Imaging Format)"; Flags: unchecked
Name: "esd"; Description: "Associate with .esd files (Electronic Software Download)"; Flags: unchecked
#ifdef SignedIdentity
Name: "explorer"; Description: "Enable Windows 11 Explorer commands"; Flags: unchecked
#endif

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "portable.flag,data\*,*.pdb"

[Icons]
Name: "{autoprograms}\VYNX ARC"; Filename: "{app}\VynxArc.exe"; Tasks: startmenu
Name: "{autodesktop}\VYNX ARC"; Filename: "{app}\VynxArc.exe"; Tasks: desktop

[Registry]
Root: HKCU; Subkey: "Software\Classes\VynxArc.Archive"; ValueType: string; ValueData: "VYNX ARC archive"; Flags: uninsdeletekey; Tasks: openwith or iso or wim or esd
Root: HKCU; Subkey: "Software\Classes\VynxArc.Archive\DefaultIcon"; ValueType: string; ValueData: "{app}\VynxArc.exe,0"; Tasks: openwith or iso or wim or esd
Root: HKCU; Subkey: "Software\Classes\VynxArc.Archive\shell\open\command"; ValueType: string; ValueData: """{app}\VynxArc.exe"" ""%1"""; Tasks: openwith or iso or wim or esd
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
Root: HKCU; Subkey: "Software\Classes\.wim\OpenWithProgids"; ValueType: string; ValueName: "VynxArc.Archive"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.esd\OpenWithProgids"; ValueType: string; ValueName: "VynxArc.Archive"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.swm\OpenWithProgids"; ValueType: string; ValueName: "VynxArc.Archive"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.iso"; ValueType: string; ValueData: "VynxArc.Archive"; Flags: uninsdeletevalue; Tasks: iso
Root: HKCU; Subkey: "Software\Classes\.iso\OpenWithProgids"; ValueType: string; ValueName: "VynxArc.Archive"; ValueData: ""; Flags: uninsdeletevalue; Tasks: iso
Root: HKCU; Subkey: "Software\Classes\.wim"; ValueType: string; ValueData: "VynxArc.Archive"; Flags: uninsdeletevalue; Tasks: wim
Root: HKCU; Subkey: "Software\Classes\.wim\OpenWithProgids"; ValueType: string; ValueName: "VynxArc.Archive"; ValueData: ""; Flags: uninsdeletevalue; Tasks: wim
Root: HKCU; Subkey: "Software\Classes\.esd"; ValueType: string; ValueData: "VynxArc.Archive"; Flags: uninsdeletevalue; Tasks: esd
Root: HKCU; Subkey: "Software\Classes\.esd\OpenWithProgids"; ValueType: string; ValueName: "VynxArc.Archive"; ValueData: ""; Flags: uninsdeletevalue; Tasks: esd

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
