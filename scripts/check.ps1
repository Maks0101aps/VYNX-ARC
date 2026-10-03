param([string]$QtRoot = $env:VYNX_QT_ROOT)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'env.ps1') -QtRoot $QtRoot
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    Invoke-VynxTool cargo @('fmt','--all','--','--check')
    Invoke-VynxTool cargo @('clippy','--workspace','--all-targets','--locked','--','-D','warnings')
    Invoke-VynxTool cargo @('test','--workspace','--locked')
    Invoke-VynxTool cargo @('audit')
    Invoke-VynxTool cmake @('-S','.', '-B','build-msvc','-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER=cl',"-DCMAKE_PREFIX_PATH=$env:VYNX_QT_ROOT")
    Invoke-VynxTool cmake @('--build','build-msvc')
    Invoke-VynxTool ctest @('--test-dir','build-msvc','--output-on-failure')
} finally { Pop-Location }
