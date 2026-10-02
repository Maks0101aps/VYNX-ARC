$ErrorActionPreference='Stop'
Get-AppxPackage -Name 'VYNX.ARC.Explorer' | Remove-AppxPackage -ErrorAction Stop
