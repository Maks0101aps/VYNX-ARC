param(
    [string]$SourceDirectory = (Join-Path $PSScriptRoot '..\dist\0.9.0'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\dist\0.9.0-test-signed'),
    [string]$CertificateThumbprint,
    [string]$ReportPath = (Join-Path $PSScriptRoot '..\docs\verification\0.9\test-signing.json')
)
# Development signature only. No trusted-store writes, key export, or registration.
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$source = (Resolve-Path -LiteralPath $SourceDirectory).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw "Output already exists; preserve it and choose a new directory: $output" }
$signtool = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe'
$iscc = Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'
foreach ($tool in $signtool,$iscc) { if (!(Test-Path -LiteralPath $tool)) { throw "Missing tool: $tool" } }
$names = @('VYNX-ARC-Portable-x64.zip','VYNX-ARC-Setup-x64.exe','VYNX-ARC-Explorer-x64.msix')
$baseline = @($names | ForEach-Object { [ordered]@{name=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $source $_)).Hash.ToLower()} })
New-Item -ItemType Directory -Path $output | Out-Null
$stage = Join-Path $output 'portable'
Expand-Archive -LiteralPath (Join-Path $source $names[0]) -DestinationPath $stage
$own = @('VynxArc.exe','vynxarc-cli.exe','vynxarc-worker.exe','VynxShell.dll','VYNX-ARC-Explorer-x64.msix')
$untouched = @{}
Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object {
    $relative = $_.FullName.Substring($stage.Length+1)
    if ($relative -notin $own) { $untouched[$relative] = (Get-FileHash -LiteralPath $_.FullName).Hash }
}
$friendly = 'VYNX ARC TESTONLY local development (30 days)'
if ($CertificateThumbprint) {
    if ($CertificateThumbprint -notmatch '^[0-9A-Fa-f]{40}$') { throw 'An exact SHA1 certificate thumbprint is required.' }
    $cert = Get-Item -LiteralPath "Cert:\CurrentUser\My\$CertificateThumbprint"
    if ($cert.Subject -ne 'CN=VYNX ARC' -or $cert.FriendlyName -ne $friendly -or !$cert.HasPrivateKey -or $cert.NotAfter -le (Get-Date)) { throw 'Certificate is not the exact owned, valid TESTONLY signing identity.' }
} else {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=VYNX ARC' -FriendlyName $friendly -CertStoreLocation 'Cert:\CurrentUser\My' -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 -KeyExportPolicy NonExportable -NotAfter (Get-Date).AddDays(30)
}
$rsa = [System.Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPrivateKey($cert)
try {
    if ($rsa -isnot [System.Security.Cryptography.RSACng] -or $rsa.Key.ExportPolicy -ne [System.Security.Cryptography.CngExportPolicies]::None) { throw 'Expected a nonexportable CNG private key.' }
} finally { if ($rsa) { $rsa.Dispose() } }
$certRecord = [ordered]@{subject=$cert.Subject;thumbprint=$cert.Thumbprint;friendly_name=$cert.FriendlyName;not_before_utc=$cert.NotBefore.ToUniversalTime().ToString('o');not_after_utc=$cert.NotAfter.ToUniversalTime().ToString('o');private_key='CurrentUser/My; nonexportable; never exported';trusted_store_imports=$false;timestamp=$false}
$certRecord | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'test-certificate.json') -Encoding UTF8
$chain=New-Object Security.Cryptography.X509Certificates.X509Chain
try {
    $chainTrusted=$chain.Build($cert)
    $chainStatuses=@($chain.ChainStatus | ForEach-Object {$_.Status.ToString()})
    if($chainTrusted -or $chainStatuses.Count -ne 1 -or $chainStatuses[0] -ne 'UntrustedRoot'){throw "Expected only UntrustedRoot: $chainStatuses"}
} finally {$chain.Dispose()}
Export-Certificate -Cert $cert -FilePath (Join-Path $output 'VYNX-ARC-TESTONLY.cer') -Type CERT | Out-Null
function Invoke-Native([string]$Tool,[string[]]$Arguments) {
    $previous = $ErrorActionPreference
    try { $ErrorActionPreference='Continue'; $log=@(& $Tool @Arguments 2>&1 | ForEach-Object {$_.ToString()}); $code=$LASTEXITCODE }
    finally { $ErrorActionPreference=$previous }
    if ($code) { throw "$Tool exited $code`n$($log -join "`n")" }
    $log
}
function Sign-File([string]$Path) {
    Invoke-Native $signtool @('sign','/s','My','/sha1',$cert.Thumbprint,'/fd','SHA256','/d','VYNX ARC TESTONLY - untrusted local test',$Path) | Out-Host
}
foreach ($name in $own) { Sign-File (Join-Path $stage $name) }
Copy-Item -LiteralPath (Join-Path $stage $names[2]) -Destination (Join-Path $output $names[2])
# Deliberately omit /DSignedIdentity: this test certificate is not trusted.
Invoke-Native $iscc @("/DStageDir=$stage","/DOutputDir=$output",(Join-Path $repo 'installer\VynxArc.iss')) | Set-Content -LiteralPath (Join-Path $output 'inno-build.log')
Sign-File (Join-Path $output $names[1])
Add-Type -AssemblyName System.Security
Add-Type -AssemblyName System.IO.Compression.FileSystem
function Read-SignedCms([string]$Path) {
    if ([IO.Path]::GetExtension($Path) -eq '.msix') {
        $zip=[IO.Compression.ZipFile]::OpenRead($Path)
        try {
            $stream=$zip.GetEntry('AppxSignature.p7x').Open(); $memory=New-Object IO.MemoryStream
            try { $stream.CopyTo($memory); $bytes=$memory.ToArray() } finally { $stream.Dispose();$memory.Dispose() }
            if ([Text.Encoding]::ASCII.GetString($bytes,0,4) -ne 'PKCX') { throw 'Invalid MSIX signature header' }
            $blob=$bytes[4..($bytes.Length-1)]
        } finally { $zip.Dispose() }
    } else {
        $bytes=[IO.File]::ReadAllBytes($Path);$pe=[BitConverter]::ToInt32($bytes,0x3c);$optional=$pe+24
        $directory=if([BitConverter]::ToUInt16($bytes,$optional)-eq 0x20b){$optional+144}else{$optional+128}
        $offset=[BitConverter]::ToInt32($bytes,$directory);$length=[BitConverter]::ToInt32($bytes,$offset)
        if($offset -le 0 -or $length -le 8){throw 'Missing PE certificate table'}
        $blob=$bytes[($offset+8)..($offset+$length-1)]
    }
    $cms=New-Object Security.Cryptography.Pkcs.SignedCms
    $cms.Decode([byte[]]$blob);$cms.CheckSignature($true)
    if($cms.SignerInfos.Count -ne 1 -or $cms.SignerInfos[0].Certificate.Thumbprint -ne $cert.Thumbprint){throw 'Unexpected CMS signer'}
    if($cms.SignerInfos[0].DigestAlgorithm.Value -ne '2.16.840.1.101.3.4.2.1'){throw 'Expected SHA256 CMS digest'}
    return $cms
}
$signatures=@()
foreach($path in (@($own | ForEach-Object { Join-Path $stage $_ }) + @(Join-Path $output $names[1]))) {
    $cms=Read-SignedCms $path
    $signature=Get-AuthenticodeSignature -LiteralPath $path
    # Windows PowerShell 5 maps CERT_E_UNTRUSTEDROOT to UnknownError.
    if($signature.Status.ToString() -notin @('NotTrusted','UnknownError') -or $signature.StatusMessage -notmatch 'root certificate which is not trusted' -or $signature.SignerCertificate.Thumbprint -ne $cert.Thumbprint){throw "Unexpected Windows signature result for $path : $($signature.Status)"}
    $previous=$ErrorActionPreference
    try { $ErrorActionPreference='Continue';$verify=@(& $signtool verify /pa /v $path 2>&1 | ForEach-Object {$_.ToString()});$verifyCode=$LASTEXITCODE }
    finally { $ErrorActionPreference=$previous }
    if($verifyCode -ne 1 -or ($verify -join "`n") -notmatch 'root\s+certificate which is not trusted'){throw "Expected untrusted-root failure: $($verify -join "`n")"}
    $signatures += [ordered]@{file=$path.Substring($output.Length+1);sha256=(Get-FileHash -LiteralPath $path).Hash.ToLower();authenticode_status=$signature.Status.ToString();status_message=$signature.StatusMessage;cms_signature_valid=$true;cms_digest='SHA256';signtool_verify_exit=$verifyCode;signtool_output=($verify -join "`n");certificate_chain_status=$chainStatuses;timestamp_present=($null -ne $signature.TimeStamperCertificate)}
}
# Change a byte inside a PE section, never the source or signed release copy.
$tamper=Join-Path $output 'tampered-cli-negative-control.exe'
$bytes=[IO.File]::ReadAllBytes((Join-Path $stage 'vynxarc-cli.exe'))
$pe=[BitConverter]::ToInt32($bytes,0x3c);$section=$pe+24+[BitConverter]::ToUInt16($bytes,$pe+20)
$raw=[BitConverter]::ToInt32($bytes,$section+20);$bytes[$raw]=$bytes[$raw] -bxor 1
[IO.File]::WriteAllBytes($tamper,$bytes)
$tamperStatus=(Get-AuthenticodeSignature -LiteralPath $tamper).Status.ToString()
if($tamperStatus -ne 'HashMismatch'){throw "Tamper was not rejected: $tamperStatus"}
# Reject any change to dependencies, scripts, licenses, or other original payload.
foreach($relative in $untouched.Keys){if((Get-FileHash -LiteralPath (Join-Path $stage $relative)).Hash -ne $untouched[$relative]){throw "Unexpected payload change: $relative"}}
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath (Join-Path $output $names[0])
$hashes=@($names | ForEach-Object { [ordered]@{name=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $output $_)).Hash.ToLower()} })
$hashes | ForEach-Object { "$($_.sha256)  $($_.name)" } | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding ASCII
# Smoke the final ZIP's extracted signed binaries with a minimal runtime PATH.
$smoke=Join-Path $output 'smoke-extracted'
Expand-Archive -LiteralPath (Join-Path $output $names[0]) -DestinationPath $smoke
$oldPath=$env:PATH;$oldQpa=$env:QT_QPA_PLATFORM;$oldConsole=$env:QT_ASSUME_STDERR_HAS_CONSOLE
$checks=@()
try {
    $env:PATH="$env:SystemRoot\System32;$env:SystemRoot";$env:QT_QPA_PLATFORM='windows';$env:QT_ASSUME_STDERR_HAS_CONSOLE='1'
    foreach($arguments in @(@('--smoke-test'),@('--smoke-test','--product-smoke','-'),@('--smoke-test','--worker-fault-smoke','batch','-'))){
        $log=Invoke-Native (Join-Path $smoke 'VynxArc.exe') $arguments
        $checks += [ordered]@{command="VynxArc.exe $($arguments -join ' ')";exit_code=0;output=($log -join "`n")}
    }
    $log=Invoke-Native (Join-Path $smoke 'vynxarc-cli.exe') @('--help')
    $checks += [ordered]@{command='vynxarc-cli.exe --help';exit_code=0;output=($log -join "`n")}
    $work=Join-Path $output 'cli-roundtrip'
    New-Item -ItemType Directory -Path $work | Out-Null
    $inputFile=Join-Path $work 'payload.txt';$archive=Join-Path $work 'signed-smoke.zip';$extracted=Join-Path $work 'extracted'
    [IO.File]::WriteAllText($inputFile,'Signed VYNX ARC 0.9.0 CLI roundtrip payload.')
    foreach($arguments in @(@('create',$archive,$inputFile),@('test',$archive),@('extract',$archive,'--output',$extracted))){
        $log=Invoke-Native (Join-Path $smoke 'vynxarc-cli.exe') $arguments
        $checks += [ordered]@{command="vynxarc-cli.exe $($arguments -join ' ')";exit_code=0;output=($log -join "`n")}
    }
    if((Get-FileHash -LiteralPath $inputFile).Hash -ne (Get-FileHash -LiteralPath (Join-Path $extracted 'payload.txt')).Hash){throw 'Signed CLI roundtrip bytes differ'}
} finally {$env:PATH=$oldPath;$env:QT_QPA_PLATFORM=$oldQpa;$env:QT_ASSUME_STDERR_HAS_CONSOLE=$oldConsole}
foreach($entry in $baseline){if((Get-FileHash -LiteralPath (Join-Path $source $entry.name)).Hash.ToLower() -ne $entry.sha256){throw "Unsigned baseline changed: $($entry.name)"}}
$report=[ordered]@{status='passed-development-signing-untrusted';timestamp_utc=[DateTime]::UtcNow.ToString('o');certificate=$certRecord;unsigned_source=$source;unsigned_baseline_unchanged=$true;unsigned_baseline=$baseline;output_directory=$output;artifacts=$hashes;signatures=$signatures;tamper_negative_control=[ordered]@{file=$tamper;status=$tamperStatus;executed=$false};untouched_payload_files=$untouched.Count;smoke=$checks;installer=[ordered]@{rebuilt_from_signed_stage=$true;outer_exe_signed=$true;signed_identity_flag=$false;installed_test=$false};limitations=@('Self-signed certificate is not publicly trusted; Windows verification intentionally fails for an untrusted root.','No timestamp: test signatures expire with the 30-day certificate.','CMS CheckSignature(true) checks cryptographic signature, not a trusted chain. Windows separately checks Authenticode file integrity; tampering returns HashMismatch.','No trust-store import, Explorer registration, SmartScreen acceptance, clean-Windows acceptance, or installer execution is claimed.')}
New-Item -ItemType Directory -Force (Split-Path ([IO.Path]::GetFullPath($ReportPath)) -Parent) | Out-Null
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $ReportPath -Encoding UTF8
Write-Host "TESTONLY artifacts: $output"
Write-Host "Evidence: $ReportPath"
