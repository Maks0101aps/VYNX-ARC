param([string]$Cli = (Join-Path $PSScriptRoot '..\target\release\vynxarc-cli.exe'))
$ErrorActionPreference = 'Stop'
$Cli = (Resolve-Path -LiteralPath $Cli).Path
$case = Join-Path ([IO.Path]::GetTempPath()) ('vynx-permission-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $case | Out-Null
$sourceFile = Join-Path $case 'file.txt'
$archive = Join-Path $case 'archive.zip'
Set-Content -LiteralPath $sourceFile -Value 'original content'
& $Cli create $archive $sourceFile
if ($LASTEXITCODE) { throw 'Permission fixture creation failed' }
$before = (Get-FileHash -LiteralPath $archive).Hash
$user = [Security.Principal.WindowsIdentity]::GetCurrent().Name
try {
    & icacls.exe $archive /deny "${user}:(D)"
    if ($LASTEXITCODE) { throw 'Could not deny DELETE on isolated fixture' }
    & icacls.exe $case /deny "${user}:(DC)"
    if ($LASTEXITCODE) { throw 'Could not deny DELETE_CHILD on isolated parent' }
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $messages = & $Cli rename $archive file.txt renamed.txt 2>&1
        $exitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
} finally {
    # Only these newly created fixtures were given explicit denial ACEs.
    & icacls.exe $archive /remove:d $user
    if ($LASTEXITCODE) { throw 'Could not restore isolated archive ACL' }
    & icacls.exe $case /remove:d $user
    if ($LASTEXITCODE) { throw 'Could not restore isolated parent ACL' }
}
$after = (Get-FileHash -LiteralPath $archive).Hash
[ordered]@{case=$case; exitCode=$exitCode; diagnostics=($messages | Out-String); originalPreserved=($before -eq $after)} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $case 'results.json') -Encoding UTF8
if ($exitCode -ne 1 -or $before -ne $after) { throw 'Permission rejection/preservation check failed' }
Write-Host "Permission failure preserved original: $case"
