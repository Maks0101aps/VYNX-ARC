param([string]$QtRoot = $env:VYNX_QT_ROOT)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'env.ps1') -QtRoot $QtRoot
foreach ($tool in 'cargo','rustc','cmake','ninja','g++','windeployqt') {
    $command = Get-Command $tool -ErrorAction SilentlyContinue
    if (!$command) { throw "Missing prerequisite: $tool. See docs/BUILD.md." }
    Write-Host "$tool : $($command.Source)"
}
Write-Host 'Toolchain detected. No system settings were changed.'
