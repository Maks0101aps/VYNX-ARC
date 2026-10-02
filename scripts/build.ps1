param([string]$QtRoot = $env:VYNX_QT_ROOT)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'env.ps1') -QtRoot $QtRoot
$repoRoot = Split-Path $PSScriptRoot -Parent
Push-Location $repoRoot
try {
    Invoke-VynxTool cargo @('test','--workspace','--locked')
    Invoke-VynxTool cmake @('-S','.', '-B','build','-G','Ninja','-DCMAKE_BUILD_TYPE=Release',"-DCMAKE_PREFIX_PATH=$env:VYNX_QT_ROOT")
    Invoke-VynxTool cmake @('--build','build')
    Invoke-VynxTool ctest @('--test-dir','build','--output-on-failure')
} finally { Pop-Location }
