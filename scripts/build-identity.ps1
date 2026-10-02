param([string]$OutputDirectory=(Join-Path $PSScriptRoot '..\dist'))
$ErrorActionPreference='Stop'
if (!(Get-Command Invoke-VynxTool -ErrorAction SilentlyContinue)) { . (Join-Path $PSScriptRoot 'env.ps1') }
$repoRoot=Split-Path $PSScriptRoot -Parent
$sdk=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin\10.0.26100.0\x64'
$identityDirectory=Join-Path $OutputDirectory ('identity-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force (Join-Path $identityDirectory 'Assets') | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot 'installer\identity\AppxManifest.xml') -Destination $identityDirectory
Copy-Item -LiteralPath (Join-Path $repoRoot 'assets\icons\vynx-arc.png') -Destination (Join-Path $identityDirectory 'Assets\Logo.png')
$package=Join-Path $OutputDirectory 'VYNX-ARC-Explorer-x64.msix'
# /nv is required by Microsoft's sparse-package instructions: the binary paths
# resolve against ExternalLocation at registration, not inside this identity ZIP.
Invoke-VynxTool (Join-Path $sdk 'makeappx.exe') @('pack','/o','/d',$identityDirectory,'/nv','/p',$package)
if ($env:VYNX_SIGN_SCRIPT) { & $env:VYNX_SIGN_SCRIPT $package; if($LASTEXITCODE){throw 'Identity signing failed'} }
Write-Host "Identity package: $package"
