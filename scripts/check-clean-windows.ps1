param([string]$OutputDirectory, [switch]$PrepareOnly)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(!$OutputDirectory){$OutputDirectory=Join-Path $root ('.dev\clean-windows-'+[guid]::NewGuid().ToString('N'))}
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $output){throw 'Evidence directory must be new'}
New-Item -ItemType Directory $output | Out-Null
$payload=Join-Path $output 'input'
$evidence=Join-Path $output 'evidence'
New-Item -ItemType Directory $payload,$evidence | Out-Null
foreach($name in 'VYNX-ARC-Portable-x64.zip','VYNX-ARC-Setup-x64.exe','VYNX-ARC-Explorer-x64.msix','SHA256SUMS.txt') {
    Copy-Item -LiteralPath (Join-Path $root "dist\$name") -Destination $payload
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'clean-windows-guest.ps1') -Destination (Join-Path $payload 'guest.ps1')
$identity=[ordered]@{prepared_utc=[DateTime]::UtcNow.ToString('o');git_head=(& git -C $root rev-parse HEAD);dirty_files=(& git -C $root status --porcelain);status='PREPARED_NOT_RUN'}
$identity | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $output 'host.json')
$payloadXml=[Security.SecurityElement]::Escape($payload)
$evidenceXml=[Security.SecurityElement]::Escape($evidence)
$configuration=@"
<Configuration>
  <Networking>Disable</Networking>
  <ClipboardRedirection>Disable</ClipboardRedirection>
  <vGPU>Disable</vGPU>
  <MappedFolders>
    <MappedFolder><HostFolder>$payloadXml</HostFolder><SandboxFolder>C:\VynxInput</SandboxFolder><ReadOnly>true</ReadOnly></MappedFolder>
    <MappedFolder><HostFolder>$evidenceXml</HostFolder><SandboxFolder>C:\VynxEvidence</SandboxFolder><ReadOnly>false</ReadOnly></MappedFolder>
  </MappedFolders>
  <LogonCommand><Command>powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\VynxInput\guest.ps1</Command></LogonCommand>
</Configuration>
"@
$wsb=Join-Path $output 'acceptance.wsb'
$configuration | Set-Content -LiteralPath $wsb -Encoding UTF8
Write-Host "Prepared clean Windows run: $wsb"
if(!$PrepareOnly) {
    try {
        $hypervisorPresent=(Get-CimInstance Win32_ComputerSystem -ErrorAction Stop).HypervisorPresent
        $identity.hypervisor_present=$hypervisorPresent
        if(!$hypervisorPresent) {
            $identity.status='BLOCKED_HYPERVISOR_NOT_RUNNING'
            $identity.error='Windows reports HypervisorPresent=False. Prepared guest checks have NOT run. Review Windows Sandbox virtualization prerequisites before retrying; this script does not change boot or security settings.'
            $identity | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $output 'host.json')
            Write-Warning $identity.error
            return
        }
        $process=Start-Process -FilePath 'WindowsSandbox.exe' -ArgumentList ('"'+$wsb+'"') -PassThru -WindowStyle Hidden
        $identity.status='LAUNCH_REQUESTED_NOT_VERIFIED'
        $identity.process_id=$process.Id
    } catch {
        $identity.status='LAUNCH_FAILED'
        $identity.error=$_.Exception.Message
    }
    $identity | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $output 'host.json')
}
