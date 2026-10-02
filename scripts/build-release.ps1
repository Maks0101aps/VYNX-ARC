param([string]$QtRoot=$env:VYNX_QT_ROOT, [switch]$SkipInstaller)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'env.ps1') -QtRoot $QtRoot
$repoRoot=Split-Path $PSScriptRoot -Parent
Push-Location $repoRoot
try {
    & (Join-Path $PSScriptRoot 'build.ps1') -QtRoot $env:VYNX_QT_ROOT
    $distRoot=Join-Path $repoRoot 'dist'
    New-Item -ItemType Directory -Force $distRoot | Out-Null
    $stage=Join-Path $distRoot ('stage-'+[guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory $stage | Out-Null
    Copy-Item -LiteralPath (Join-Path $repoRoot 'build-msvc\VynxArc.exe') -Destination $stage
    Copy-Item -LiteralPath (Join-Path $repoRoot 'build-msvc\VynxShell.dll') -Destination $stage
    & (Join-Path $PSScriptRoot 'build-identity.ps1') -OutputDirectory $distRoot
    Copy-Item -LiteralPath (Join-Path $distRoot 'VYNX-ARC-Explorer-x64.msix') -Destination $stage
    Copy-Item -LiteralPath (Join-Path $repoRoot 'installer\Register-Explorer.ps1'),(Join-Path $repoRoot 'installer\Unregister-Explorer.ps1') -Destination $stage
    Copy-Item -LiteralPath (Join-Path $repoRoot 'target\release\vynxarc-cli.exe') -Destination $stage
    Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE'),(Join-Path $repoRoot 'docs\THIRD_PARTY_NOTICES.md') -Destination $stage
    Invoke-VynxTool windeployqt @('--release','--no-translations','--no-system-d3d-compiler','--no-opengl-sw','--no-compiler-runtime','--skip-plugin-types','generic,networkinformation,tls',(Join-Path $stage 'VynxArc.exe'))
    # App-local MSVC runtime: no developer SDK or runtime installation needed.
    $crt = Get-ChildItem -LiteralPath (Join-Path $vsRoot 'VC\Redist\MSVC') -Directory |
        Where-Object Name -Match '^\d+\.' | Sort-Object Name -Descending | Select-Object -First 1
    $crtDirectory = Join-Path $crt.FullName 'x64\Microsoft.VC143.CRT'
    if (!(Test-Path $crtDirectory)) { throw 'MSVC redistributable DLLs not found.' }
    Get-ChildItem -LiteralPath $crtDirectory -Filter '*.dll' | Copy-Item -Destination $stage
    New-Item -ItemType Directory -Force (Join-Path $stage 'translations') | Out-Null
    foreach($lang in 'uk','ru') {
        Copy-Item -LiteralPath (Join-Path $env:VYNX_QT_ROOT "translations\qtbase_$lang.qm") -Destination (Join-Path $stage 'translations')
    }
    $licenseRoot=Join-Path $stage 'licenses'
    New-Item -ItemType Directory -Force $licenseRoot | Out-Null
    Copy-Item -LiteralPath (Join-Path $vsRoot 'Licenses\1033\Redist.txt') -Destination (Join-Path $licenseRoot 'MSVC-Redist.txt')
    Invoke-VynxTool cargo @('metadata','--locked','--format-version','1','--filter-platform','x86_64-pc-windows-msvc') | Where-Object { $_.StartsWith('{') } | Set-Content -Encoding UTF8 (Join-Path $licenseRoot 'CARGO_LICENSES.json')
    Invoke-VynxTool python @((Join-Path $PSScriptRoot 'collect-licenses.py'),$licenseRoot,$env:VYNX_QT_ROOT)
    # Authenticode signing is performed only after binaries and DLLs are final.
    # VYNX_SIGN_SCRIPT is a trusted locally supplied script using a secure key service.
    if($env:VYNX_SIGN_SCRIPT) { & $env:VYNX_SIGN_SCRIPT $stage; if($LASTEXITCODE){throw 'Signing failed'} }
    $env:QT_QPA_PLATFORM='windows'
    $env:QT_ASSUME_STDERR_HAS_CONSOLE='1'
    Invoke-VynxTool (Join-Path $stage 'VynxArc.exe') @('--smoke-test')
    Invoke-VynxTool (Join-Path $stage 'vynxarc-cli.exe') @('--help')
    if(!$SkipInstaller){
        $iscc=Get-Command ISCC -ErrorAction SilentlyContinue
        if(!$iscc){$path=Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe';if(Test-Path $path){$iscc=Get-Item -LiteralPath $path}}
        if(!$iscc){throw 'Inno Setup 6 is required for the installer, or use -SkipInstaller.'}
        $compilerPath = if($iscc -is [System.Management.Automation.ApplicationInfo]){$iscc.Source}else{$iscc.FullName}
        $installerArguments=@("/DStageDir=$stage","/DOutputDir=$distRoot",(Join-Path $repoRoot 'installer\VynxArc.iss'))
        if ($env:VYNX_SIGN_SCRIPT) {
            $sdk=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin\10.0.26100.0\x64'
            Invoke-VynxTool (Join-Path $sdk 'signtool.exe') @('verify','/pa',(Join-Path $stage 'VYNX-ARC-Explorer-x64.msix'))
            $installerArguments=@('/DSignedIdentity')+$installerArguments
        }
        Invoke-VynxTool $compilerPath $installerArguments
    }
    Set-Content -LiteralPath (Join-Path $stage 'portable.flag') -Value ''
    $portable=Join-Path $distRoot 'VYNX-ARC-Portable-x64.zip'
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $portable -Force
    $artifacts=Get-ChildItem -LiteralPath $distRoot -File | Where-Object Extension -In '.zip','.exe','.msix'
    $hashes=$artifacts | ForEach-Object { "$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower())  $($_.Name)" }
    $hashes | Set-Content -LiteralPath (Join-Path $distRoot 'SHA256SUMS.txt') -Encoding ASCII
    Write-Host "Runnable staging directory: $stage"
    Write-Host "Portable release: $portable"
} finally {Remove-Item Env:\QT_QPA_PLATFORM,Env:\QT_ASSUME_STDERR_HAS_CONSOLE -ErrorAction SilentlyContinue;Pop-Location}
