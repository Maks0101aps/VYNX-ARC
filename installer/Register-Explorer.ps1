param([Parameter(Mandatory=$true)][string]$InstallDirectory)
$ErrorActionPreference='Stop'
$location=(Resolve-Path -LiteralPath $InstallDirectory).Path
if (Test-Path -LiteralPath (Join-Path $location 'portable.flag')) { throw 'Portable mode never registers Explorer integration.' }
$package=Join-Path $location 'VYNX-ARC-Explorer-x64.msix'
if (!(Test-Path -LiteralPath (Join-Path $location 'VynxShell.dll'))) { throw 'Shell component is missing.' }
# Let the Windows deployment service validate publisher/signature/trust. This
# script never imports certificates or changes Developer Mode/security settings.
Add-AppxPackage -Path $package -ExternalLocation $location -ErrorAction Stop
