param(
    [Parameter(Mandatory=$true)][string]$EvidenceName,
    [switch]$ReopenCode
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $repo
$evidenceRoot = Join-Path $repo '.dev/030-publication-root-cause'
if ($EvidenceName -notmatch '^off[0-9]+$') { throw 'Expected an offN evidence name' }
$python = (Get-Command python).Source
$codeExe = Join-Path $env:LOCALAPPDATA 'Programs/Microsoft VS Code/Code.exe'
try {
    # Normal close requests allow the editor to prompt for unsaved work.
    # Never force termination. No OFF workload is valid with Code still present.
    for ($attempt = 0; $attempt -lt 15; $attempt++) {
        $codeProcesses = @(Get-Process Code -ErrorAction SilentlyContinue)
        if ($codeProcesses.Count -eq 0) { break }
        foreach ($editor in $codeProcesses) {
            if ($editor.MainWindowHandle -ne 0) { $null = $editor.CloseMainWindow() }
        }
        Start-Sleep -Seconds 2
    }
    $before = @(Get-CimInstance Win32_Process | Where-Object {
        $_.Name -eq 'Code.exe' -or ($_.Name -match '^(node|python|pyright).*\.exe$' -and $_.CommandLine -match 'pylance|server\.bundle\.js')
    } | Select-Object Name,ProcessId,ParentProcessId,ExecutablePath,CommandLine)
    ConvertTo-Json -InputObject $before -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $evidenceRoot "$EvidenceName-processes-before.json")
    if ($before.Count -ne 0) { throw 'OFF control blocked: editor/language-server processes remain; no forced close performed' }
    $ErrorActionPreference = 'Continue'
    & $python scripts/stress-zip-publication.py --location project --workspace .dev/zip-stress-controlled --evidence ".dev/030-publication-root-cause/$EvidenceName" --continue-on-failure *> (Join-Path $evidenceRoot "$EvidenceName-console.log")
    $stressExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    $after = @(Get-CimInstance Win32_Process | Where-Object { $_.Name -eq 'Code.exe' } | Select-Object Name,ProcessId,ParentProcessId,ExecutablePath,CommandLine)
    ConvertTo-Json -InputObject $after -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $evidenceRoot "$EvidenceName-processes-after.json")
    [pscustomobject]@{ExitCode=$stressExit;NoCodeBefore=($before.Count -eq 0);NoCodeAfter=($after.Count -eq 0);FinishedUtc=(Get-Date).ToUniversalTime().ToString('o')} | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $evidenceRoot "$EvidenceName-control.json")
} catch {
    $_ | Out-String | Set-Content -Encoding UTF8 (Join-Path $evidenceRoot "$EvidenceName-controller-error.log")
} finally {
    if ($ReopenCode) { Start-Process -FilePath $codeExe -ArgumentList ('"' + $repo + '"') -WindowStyle Hidden }
}
