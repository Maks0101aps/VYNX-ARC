param([Parameter(Mandatory=$true)][string]$EvidenceRoot)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $repo
$EvidenceRoot = (Resolve-Path -LiteralPath $EvidenceRoot).Path
$python = (Get-Command python).Source
$cli = Join-Path $repo '.dev/zip-stress-controlled/app/vynxarc-cli.exe'
$portable = Join-Path $repo 'dist/VYNX-ARC-Portable-x64.zip'
function Get-RelatedProcesses {
    @(Get-CimInstance Win32_Process | Where-Object {
        $_.Name -eq 'Code.exe' -or ($_.Name -notmatch '^(powershell|pwsh)\.exe$' -and
            $_.CommandLine -match 'vscode-server|extensionHost|[\\/]pylance[\\/]|vscode-pylance|[\\/]pyright[\\/]|pyright-langserver|Microsoft VS Code')
    } | Select-Object Name,ProcessId,ParentProcessId,ExecutablePath,CommandLine)
}
function Save-State($name) {
    $all = @(Get-CimInstance Win32_Process | Select-Object Name,ProcessId,ParentProcessId,ExecutablePath,CommandLine)
    ConvertTo-Json -InputObject $all -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $EvidenceRoot "$name-processes-all.json")
    $related = @(Get-RelatedProcesses)
    ConvertTo-Json -InputObject $related -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $EvidenceRoot "$name-processes-related.json")
    return $related
}
try {
    $identity = [pscustomobject]@{
        Head=(& git rev-parse HEAD); Cli=$cli
        CliSha256=(Get-FileHash -LiteralPath $cli).Hash.ToLowerInvariant()
        PortableSha256=(Get-FileHash -LiteralPath $portable).Hash.ToLowerInvariant()
        StartedUtc=(Get-Date).ToUniversalTime().ToString('o')
    }
    if ($identity.CliSha256 -ne 'a7df0bd4fb7a20e7e241aeb7e224af1629f1f4015b9068d38891b8feee3f9823' -or
        $identity.PortableSha256 -ne '129ff5382aa3f4ca4849d045c9158a802a5e31db9f2b948ef4b9d723d70d3105') {
        throw 'Artifact differs from the ON experiment; refusing archive tests'
    }
    $identity | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $EvidenceRoot 'identity.json')
    $deadline = (Get-Date).AddMinutes(15)
    # Hold the diagnostic log open; reopening it on each poll can race an editor
    # reading the evidence directory. This never changes archive publication.
    $waitStream = [IO.FileStream]::new((Join-Path $EvidenceRoot 'wait-for-off.jsonl'), [IO.FileMode]::Append, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
    $waitWriter = [IO.StreamWriter]::new($waitStream, [Text.UTF8Encoding]::new($false))
    $waitWriter.AutoFlush = $true
    try {
    do {
        $related = @(Get-RelatedProcesses)
        $waitWriter.WriteLine(([pscustomobject]@{TimestampUtc=(Get-Date).ToUniversalTime().ToString('o');Count=$related.Count;Processes=$related} | ConvertTo-Json -Depth 4 -Compress))
        if ($related.Count -eq 0) { break }
        if ((Get-Date) -gt $deadline) { throw 'VS Code/Pylance remain present; no tests started' }
        Start-Sleep -Seconds 2
    } while ($true)
    } finally { $waitWriter.Dispose() }
    for ($runNumber = 1; $runNumber -le 2; $runNumber++) {
        $name = "off$runNumber"
        $before = @(Save-State "$name-before")
        if ($before.Count -ne 0) { throw "Invalid OFF state before $name; archive test not started" }
        $watcher = Register-WmiEvent -Query "SELECT * FROM Win32_ProcessStartTrace WHERE ProcessName = 'Code.exe'" -SourceIdentifier "VynxCodeStarts-$runNumber"
        $monitor = Start-Job -ArgumentList $EvidenceRoot,$name -ScriptBlock {
            param($root,$runName)
            while (-not (Test-Path -LiteralPath (Join-Path $root "$runName-monitor-stop"))) {
                $related = @(Get-CimInstance Win32_Process | Where-Object {
                    $_.Name -eq 'Code.exe' -or ($_.Name -notmatch '^(powershell|pwsh)\.exe$' -and
                        $_.CommandLine -match 'vscode-server|extensionHost|[\\/]pylance[\\/]|vscode-pylance|[\\/]pyright[\\/]|pyright-langserver|Microsoft VS Code')
                } | Select-Object Name,ProcessId,ParentProcessId,ExecutablePath,CommandLine)
                [pscustomobject]@{TimestampUtc=(Get-Date).ToUniversalTime().ToString('o');Count=$related.Count;Processes=$related} | ConvertTo-Json -Depth 4 -Compress | Add-Content -Encoding UTF8 (Join-Path $root "$runName-process-monitor.jsonl")
                Start-Sleep -Seconds 1
            }
        }
        try {
            $ErrorActionPreference = 'Continue'
            & $python scripts/stress-zip-publication.py --location project --cli $cli --evidence (Join-Path $EvidenceRoot $name) --continue-on-failure *> (Join-Path $EvidenceRoot "$name-console.log")
            $stressExit = $LASTEXITCODE
            $ErrorActionPreference = 'Stop'
        } finally {
            New-Item -ItemType File -Path (Join-Path $EvidenceRoot "$name-monitor-stop") | Out-Null
            $null = Wait-Job $monitor -Timeout 10
            Receive-Job $monitor *> (Join-Path $EvidenceRoot "$name-monitor-job.log")
            Remove-Job $monitor -Force
            $starts = @(Get-Event -SourceIdentifier "VynxCodeStarts-$runNumber" -ErrorAction SilentlyContinue | ForEach-Object {
                [pscustomobject]@{ProcessId=$_.SourceEventArgs.NewEvent.ProcessID;ProcessName=$_.SourceEventArgs.NewEvent.ProcessName;TimestampUtc=$_.TimeGenerated.ToUniversalTime().ToString('o')}
            })
            ConvertTo-Json -InputObject $starts | Set-Content -Encoding UTF8 (Join-Path $EvidenceRoot "$name-code-start-events.json")
            Unregister-Event -SourceIdentifier "VynxCodeStarts-$runNumber"
        }
        $after = @(Save-State "$name-after")
        $observations = @(Get-Content -LiteralPath (Join-Path $EvidenceRoot "$name-process-monitor.jsonl") | ForEach-Object { $_ | ConvertFrom-Json })
        $valid = $before.Count -eq 0 -and $after.Count -eq 0 -and $starts.Count -eq 0 -and
            $observations.Count -gt 0 -and @($observations | Where-Object {$_.Count -ne 0}).Count -eq 0
        [pscustomobject]@{ExitCode=$stressExit;OffStateValid=$valid;ProcessSamples=$observations.Count;CodeStartEvents=$starts.Count;FinishedUtc=(Get-Date).ToUniversalTime().ToString('o')} | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $EvidenceRoot "$name-control.json")
        if (-not $valid) { throw "$name invalid: editor/server presence observed during workload" }
    }
    'OFF matrices complete; no editor was reopened by this runner' | Set-Content -Encoding UTF8 (Join-Path $EvidenceRoot 'complete.txt')
} catch {
    $_ | Out-String | Set-Content -Encoding UTF8 (Join-Path $EvidenceRoot 'controller-error.log')
    exit 1
}
