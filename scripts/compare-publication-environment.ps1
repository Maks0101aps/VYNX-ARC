param([string]$Output = (Join-Path $PSScriptRoot '..\.dev\publication-environment.json'))
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$paths = @((Join-Path $repoRoot '.dev'),[IO.Path]::GetTempPath().TrimEnd('\'))
$rows = foreach ($path in $paths) {
    $item = Get-Item -LiteralPath $path -Force
    $acl = Get-Acl -LiteralPath $path
    $ancestors = @()
    $current = $item
    while ($current) {
        $ancestors += [ordered]@{path=$current.FullName; attributes="$($current.Attributes)"; linkType=$current.LinkType; target=$current.Target}
        $current = $current.Parent
    }
    $drive = $item.PSDrive.Name
    [ordered]@{
        path=$item.FullName; pathLength=$item.FullName.Length; attributes="$($item.Attributes)"
        owner=$acl.Owner; sddl=$acl.Sddl; inheritanceProtected=$acl.AreAccessRulesProtected
        acl=@($acl.Access | ForEach-Object { [ordered]@{identity="$($_.IdentityReference)"; rights="$($_.FileSystemRights)"; type="$($_.AccessControlType)"; inherited=$_.IsInherited} })
        volume=@(Get-Volume -DriveLetter $drive | Select-Object DriveLetter,FileSystem,UniqueId,HealthStatus)
        icacls=(& icacls.exe $item.FullName 2>&1 | Out-String)
        ancestors=$ancestors
        oneDriveRoots=@($env:OneDrive,$env:OneDriveConsumer,$env:OneDriveCommercial)
    }
}
$defender = try { Get-MpPreference | Select-Object EnableControlledFolderAccess,ControlledFolderAccessProtectedFolders } catch { @{error=$_.Exception.Message} }
$status = try { Get-MpComputerStatus | Select-Object RealTimeProtectionEnabled,AntivirusEnabled } catch { @{error=$_.Exception.Message} }
[ordered]@{timestampUtc=[DateTime]::UtcNow.ToString('o'); paths=$rows; defender=$defender; defenderStatus=$status; integrity=(& whoami.exe /groups | Out-String)} |
    ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Output -Encoding UTF8
Write-Host "Environment evidence: $Output"
