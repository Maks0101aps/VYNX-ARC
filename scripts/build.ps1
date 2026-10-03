param([string]$QtRoot = $env:VYNX_QT_ROOT)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'env.ps1') -QtRoot $QtRoot
$repoRoot = Split-Path $PSScriptRoot -Parent
Push-Location $repoRoot
try {
    & (Join-Path $PSScriptRoot 'check.ps1') -QtRoot $env:VYNX_QT_ROOT
} finally { Pop-Location }
