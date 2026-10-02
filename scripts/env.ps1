param([string]$QtRoot = $env:VYNX_QT_ROOT)
$ErrorActionPreference = 'Stop'
if (!$QtRoot) { $QtRoot = Join-Path $env:LOCALAPPDATA 'VynxArcDev\Qt' }
if (!(Test-Path (Join-Path $QtRoot 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    throw 'Set VYNX_QT_ROOT to a Qt 6.12.0 MinGW x64 SDK directory.'
}
$compilerBin = Join-Path $QtRoot 'Tools\mingw1310_64\bin'
if (!(Test-Path (Join-Path $compilerBin 'g++.exe'))) { throw 'Qt-compatible MinGW 13.1 x64 compiler is required.' }
$env:PATH = "$(Join-Path $env:USERPROFILE '.cargo\bin');$compilerBin;$(Join-Path $QtRoot 'bin');$env:PATH"
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
