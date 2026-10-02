param([string]$QtRoot = $env:VYNX_QT_ROOT)
$ErrorActionPreference = 'Stop'
if (!$QtRoot) { $QtRoot = Join-Path $env:LOCALAPPDATA 'VynxArcDev\QtMSVC' }
if (!(Test-Path (Join-Path $QtRoot 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    throw 'Set VYNX_QT_ROOT to the Qt 6.12.0 MSVC 2022 x64 SDK directory.'
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path $vswhere)) { throw 'Visual Studio 2022 C++ Build Tools and Windows SDK are required.' }
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsRoot) { throw 'Install the Visual Studio C++ build workload.' }
Import-Module (Join-Path $vsRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$env:PATH = "$(Join-Path $env:USERPROFILE '.cargo\bin');$(Join-Path $QtRoot 'bin');$env:PATH"
# Select the actual compiler/linker, even if unrelated user Cargo configuration
# contains an experimental linker or cached SDK search paths.
$env:CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_LINKER = (Get-Command link.exe).Source
$env:CARGO_ENCODED_RUSTFLAGS = ''
$env:VYNX_QT_ROOT = $QtRoot
function Invoke-VynxTool {
    param([string]$Tool, [string[]]$Arguments)
    # Windows PowerShell 5 treats native stderr as ErrorRecords when redirected.
    # Keep diagnostic output, but decide success from the real process exit code.
    $previousPreference=$ErrorActionPreference
    try {
        $ErrorActionPreference='Continue'
        & $Tool @Arguments 2>&1 | ForEach-Object { $_.ToString() }
        $toolExitCode=$LASTEXITCODE
    } finally { $ErrorActionPreference=$previousPreference }
    if($toolExitCode) { throw "$Tool exited with code $toolExitCode" }
}
