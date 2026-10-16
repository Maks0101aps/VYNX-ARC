param([string]$InputDirectory='C:\VynxInput', [string]$EvidenceDirectory='C:\VynxEvidence')
$ErrorActionPreference='Stop'
if($env:USERNAME -ne 'WDAGUtilityAccount') {
    throw 'Guest acceptance runs only as Windows Sandbox WDAGUtilityAccount. Never run installer/uninstaller acceptance on the host.'
}
if(Test-Path (Join-Path $env:LOCALAPPDATA 'Programs\VYNX ARC')) {
    throw 'Acceptance requires a fresh guest without an existing VYNX ARC installation.'
}
$report = [ordered]@{
    started_utc=[DateTime]::UtcNow.ToString('o')
    windows=(Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber)
    elevated=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    checks=@()
    explorer_menu='NOT TESTED: requires trusted signed identity and interactive Explorer checks'
    previous_version_upgrade='NOT TESTED: same-version reinstall only; previous-version artifact not supplied'
}
function Check([string]$Name,[scriptblock]$Body) {
    try { & $Body; $script:report.checks += @{name=$Name;status='PASS'} }
    catch { $script:report.checks += @{name=$Name;status='FAIL';error=$_.Exception.Message}; throw }
}
function Run([string]$File,[string[]]$Arguments) {
    $process=Start-Process -FilePath $File -ArgumentList $Arguments -PassThru -Wait -WindowStyle Hidden
    if($process.ExitCode -ne 0){throw "$File exited with $($process.ExitCode)"}
}
function Require([bool]$Condition,[string]$Message) { if(!$Condition){throw $Message} }
function OpenWithValue {
    $key=Get-Item 'HKCU:\Software\Classes\.zip\OpenWithProgids' -ErrorAction SilentlyContinue
    return $key -and ($key.GetValueNames() -contains 'VynxArc.Archive')
}
$work=Join-Path $env:USERPROFILE 'VYNX acceptance тест'
$installation=Join-Path $env:LOCALAPPDATA 'Programs\VYNX ARC'
$setup=Join-Path $InputDirectory 'VYNX-ARC-Setup-x64.exe'
$originalDefaults=@{}
foreach($extension in '.zip','.7z','.tar','.tgz') {
    $key=Get-Item "HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\$extension\UserChoice" -ErrorAction SilentlyContinue
    $originalDefaults[$extension]=if($key){$key.GetValue('ProgId')}else{$null}
}
New-Item -ItemType Directory -Force $work | Out-Null
Start-Transcript -Path (Join-Path $EvidenceDirectory 'guest.log') | Out-Null
try {
    Check 'Artifacts match supplied SHA-256' {
        foreach($line in Get-Content (Join-Path $InputDirectory 'SHA256SUMS.txt')) {
            if($line -match '^([a-fA-F0-9]{64})  (.+)$') {
                $actual=(Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $InputDirectory $Matches[2])).Hash
                Require ($actual -eq $Matches[1]) "Hash mismatch: $($Matches[2])"
            } else { throw 'Invalid checksum record' }
        }
    }
    Check 'Guest has no developer tools on PATH' {
        foreach($tool in 'cargo','rustc','cl','qmake','7z') {
            Require (!(Get-Command $tool -ErrorAction SilentlyContinue)) "Unexpected developer tool: $tool"
        }
    }
    $portable=Join-Path $work 'portable'
    Expand-Archive -LiteralPath (Join-Path $InputDirectory 'VYNX-ARC-Portable-x64.zip') -DestinationPath $portable
    Check 'Portable GUI smoke, CLI and no registration' {
        Run (Join-Path $portable 'VynxArc.exe') @('--smoke-test')
        Run (Join-Path $portable 'vynxarc-cli.exe') @('--help')
        Require (!(Test-Path 'HKCU:\Software\Classes\VynxArc.Archive')) 'Portable created a ProgID'
        Require (!(OpenWithValue)) 'Portable registered Open With'
        Require (!(Get-AppxPackage -Name VYNX.ARC.Explorer)) 'Portable registered Explorer package'
    }
    Check 'Portable ZIP/7Z create modify extract and byte comparison' {
        $cli=Join-Path $portable 'vynxarc-cli.exe'
        $source=Join-Path $work 'дані.txt'
        [IO.File]::WriteAllText($source,'Clean Windows roundtrip — дані')
        foreach($ext in 'zip','7z') {
            $archive=Join-Path $work "archive.$ext"
            Run $cli @('create',('"'+$archive+'"'),('"'+$source+'"'))
            Run $cli @('rename',('"'+$archive+'"'),'дані.txt','renamed.txt')
            Run $cli @('test',('"'+$archive+'"'))
            $output=Join-Path $work "out-$ext"
            Run $cli @('extract',('"'+$archive+'"'),'--output',('"'+$output+'"'))
            Require ((Get-FileHash $source).Hash -eq (Get-FileHash (Join-Path $output 'renamed.txt')).Hash) 'Roundtrip differs'
        }
    }
    Check 'Per-user installation without optional integration' {
        Run $setup @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/TASKS=""',('/LOG="'+(Join-Path $EvidenceDirectory 'install-minimal.log')+'"'))
        Run (Join-Path $installation 'VynxArc.exe') @('--smoke-test')
        Require (!(OpenWithValue)) 'Open With enabled despite empty tasks'
        Require (!(Test-Path (Join-Path $installation 'portable.flag'))) 'Installed portable flag'
    }
    Check 'Uninstall minimal installation' {
        Run (Join-Path $installation 'unins000.exe') @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART')
        Require (!(Test-Path (Join-Path $installation 'VynxArc.exe'))) 'Executable left after uninstall'
    }
    Check 'Install with Open With' {
        Run $setup @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/TASKS="openwith,startmenu"',('/LOG="'+(Join-Path $EvidenceDirectory 'install-openwith.log')+'"'))
        Require ([bool](OpenWithValue)) 'Missing Open With value'
        $key=Get-Item 'HKCU:\Software\Classes\VynxArc.Archive\shell\open\command'
        Require ($key.GetValue('') -eq ('"'+$installation+'\VynxArc.exe" "%1"')) 'Wrong Open With command'
        Run (Join-Path $installation 'VynxArc.exe') @('--smoke-test')
    }
    Check 'Same-version reinstall and settings retention' {
        $settingsDirectory=Join-Path $env:APPDATA 'VYNX'
        New-Item -ItemType Directory -Force $settingsDirectory | Out-Null
        $settings=Join-Path $settingsDirectory 'VYNX ARC.ini'
        Set-Content -LiteralPath $settings -Value "[General]`nhistoryEnabled=false`nacceptanceMarker=retained" -Encoding UTF8
        Run $setup @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/TASKS="openwith,startmenu"')
        Require ((Get-Content $settings -Raw) -match 'acceptanceMarker=retained') 'Settings lost on reinstall'
    }
    Check 'Uninstall removes only app registration and keeps user archive' {
        Run (Join-Path $installation 'unins000.exe') @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART')
        Require (!(Test-Path (Join-Path $installation 'VynxArc.exe'))) 'Executable left after uninstall'
        Require (!(Test-Path 'HKCU:\Software\Classes\VynxArc.Archive')) 'ProgID left after uninstall'
        Require (!(OpenWithValue)) 'Open With left after uninstall'
        Require (!(Get-AppxPackage -Name VYNX.ARC.Explorer)) 'Explorer package left'
        Require (Test-Path (Join-Path $work 'archive.zip')) 'User archive removed'
        Require (!(Get-Process VynxArc,vynxarc-cli -ErrorAction SilentlyContinue)) 'App process left running'
        foreach($extension in $originalDefaults.Keys) {
            $key=Get-Item "HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\$extension\UserChoice" -ErrorAction SilentlyContinue
            $actual=if($key){$key.GetValue('ProgId')}else{$null}
            Require ($actual -eq $originalDefaults[$extension]) "Default changed: $extension"
        }
    }
    $report.status='PASS_AUTOMATED_SUBSET'
} catch {
    $report.status='FAIL'
    $report.error=$_.Exception.Message
} finally {
    $report.finished_utc=[DateTime]::UtcNow.ToString('o')
    $report | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 (Join-Path $EvidenceDirectory 'results.json')
    Stop-Transcript | Out-Null
}
