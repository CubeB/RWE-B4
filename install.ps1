# Install RWE with built-in Windows PowerShell 5.1 or newer.
param(
    [string]$Release = 'nightly',
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'RWE'),
    [string]$From,
    [switch]$SkipSetup
)
$ErrorActionPreference = 'Stop'
if ($SkipSetup -and $From) { throw 'Choose -From or -SkipSetup, not both.' }
if (-not [Environment]::Is64BitOperatingSystem -or $env:PROCESSOR_ARCHITECTURE -eq 'ARM64' -or $env:PROCESSOR_ARCHITEW6432 -eq 'ARM64') {
    throw 'RWE requires x86-64 Windows.'
}
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
$headers = @{ 'User-Agent' = 'RWE-terminal-installer' }
$tag = [Uri]::EscapeDataString($Release)
$releaseInfo = Invoke-RestMethod -Headers $headers -Uri "https://api.github.com/repos/CubeB/RWE-B4/releases/tags/$tag"
$assets = @($releaseInfo.assets | Where-Object { $_.name -like '*-Windows-Release.zip' })
if ($assets.Count -ne 1) { throw "Expected one Windows ZIP in release $Release; found $($assets.Count)." }
$asset = $assets[0]
$InstallDir = [IO.Path]::GetFullPath($InstallDir).TrimEnd('\')
New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
$stage = Join-Path $InstallDir ('.install-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
try {
    Write-Host "Downloading RWE $($releaseInfo.tag_name)..."
    $archive = Join-Path $stage 'rwe.zip'
    $savedProgress = $ProgressPreference
    try {
        $ProgressPreference = 'SilentlyContinue'
        Invoke-WebRequest -UseBasicParsing -Headers $headers -Uri $asset.browser_download_url -OutFile $archive
    } finally { $ProgressPreference = $savedProgress }
    if ((Get-Item -LiteralPath $archive).Length -ne $asset.size) { throw 'Download size mismatch.' }
    if ($asset.digest -and $asset.digest -match '^sha256:[0-9a-f]{64}$') {
        $digest = 'sha256:' + (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($digest -ne $asset.digest) { throw 'Download checksum mismatch.' }
    } else { throw 'Release has no valid SHA-256 checksum.' }
    $payload = Join-Path $stage 'payload'
    Expand-Archive -LiteralPath $archive -DestinationPath $payload
    $engines = @(Get-ChildItem -LiteralPath $payload -Filter rwe.exe -Recurse -File | Where-Object {
        Test-Path -LiteralPath (Join-Path $_.DirectoryName 'rwe_setup.exe')
    })
    if ($engines.Count -ne 1) { throw 'Release must contain one engine and setup tool.' }
    $versionName = 'asset-' + $asset.id
    $version = Join-Path $InstallDir $versionName
    if (-not (Test-Path -LiteralPath $version)) {
        $source = [IO.Path]::GetFullPath($engines[0].DirectoryName)
        if (-not $source.StartsWith(([IO.Path]::GetFullPath($stage) + '\'), [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Extracted package is outside the staging directory.'
        }
        Move-Item -LiteralPath $source -Destination $version
    }
    foreach ($entry in @(@('rwe', 'rwe.exe'), @('rwe-setup', 'rwe_setup.exe'))) {
        $launcher = Join-Path $InstallDir ($entry[0] + '.cmd')
        $body = '@echo off' + "`r`n" + '"%~dp0' + $versionName + '\' + $entry[1] + '" %*' + "`r`n"
        [IO.File]::WriteAllText($launcher, $body, [Console]::OutputEncoding)
    }
} finally {
    # Only remove the unique staging directory created by this invocation.
    $resolvedStage = [IO.Path]::GetFullPath($stage)
    if ([IO.Path]::GetDirectoryName($resolvedStage) -eq $InstallDir -and
        [IO.Path]::GetFileName($resolvedStage).StartsWith('.install-')) {
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force -ErrorAction SilentlyContinue
    }
}
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
if (@($userPath -split ';' | Where-Object { $_.TrimEnd('\') -ieq $InstallDir }).Count -eq 0) {
    [Environment]::SetEnvironmentVariable('Path', ($InstallDir + ';' + $userPath), 'User')
}
$env:Path = $InstallDir + ';' + $env:Path
$shortcutPath = Join-Path ([Environment]::GetFolderPath('Programs')) 'RWE.lnk'
$shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut($shortcutPath)
$shortcut.TargetPath = Join-Path $version 'rwe.exe'
$shortcut.WorkingDirectory = $version
$shortcut.Save()
Write-Host "RWE installed in $InstallDir"
Write-Host 'Play: rwe'
Write-Host 'Import your Total Annihilation data: rwe-setup --from "C:\GOG Games\Total Annihilation"'
# Complete game setup after installation; updates can opt out explicitly.
if ($SkipSetup) {
    Write-Host 'Game setup skipped. Run rwe-setup when ready.'
} elseif ($From) {
    & (Join-Path $version 'rwe_setup.exe') --from $From
    if ($LASTEXITCODE -ne 0) { throw "Game data import failed (exit code $LASTEXITCODE). RWE remains installed." }
} else {
    $assistant = Join-Path $version 'setup/Import-GameData.ps1'
    if (Test-Path -LiteralPath $assistant) {
        Write-Host 'Opening game setup: search automatically or browse to your Total Annihilation folder.'
        $process = Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -ArgumentList @(
            '-NoProfile', '-STA', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $assistant + '"')
        ) -NoNewWindow -Wait -PassThru
    } else {
        # Older releases may only include the command-line importer.
        $process = Start-Process -FilePath (Join-Path $version 'rwe_setup.exe') -NoNewWindow -Wait -PassThru
    }
    if ($process.ExitCode -ne 0) { throw "Game setup failed (exit code $($process.ExitCode)). RWE remains installed; run rwe-setup to retry." }
}
