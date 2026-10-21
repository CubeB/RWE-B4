# Exercise the real installation body without changing PATH or Start menu.
$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetTempPath()) ('rwe-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
try {
    $fixture = Join-Path $root 'package'
    New-Item -ItemType Directory -Path $fixture | Out-Null
    Set-Content -LiteralPath (Join-Path $fixture 'rwe.exe') -Value 'engine fixture'
    Set-Content -LiteralPath (Join-Path $fixture 'rwe_setup.exe') -Value 'setup fixture'
    $zip = Join-Path $root 'fixture.zip'
    Compress-Archive -LiteralPath $fixture -DestinationPath $zip
    $asset = [pscustomobject]@{
        id = 123; name = 'RobotWarEngine-test-Windows-Release.zip'
        browser_download_url = 'https://example.invalid/fixture.zip'
        size = (Get-Item -LiteralPath $zip).Length
        digest = 'sha256:' + (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    function Invoke-RestMethod { param($Headers, $Uri) return [pscustomobject]@{tag_name='nightly'; assets=@($asset)} }
    function Invoke-WebRequest { param([switch]$UseBasicParsing, $Headers, $Uri, $OutFile) Copy-Item -LiteralPath $zip -Destination $OutFile }
    $source = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../install.ps1') -Raw
    $body = [scriptblock]::Create(($source -split '(?m)^\$userPath =', 2)[0])
    $install = Join-Path $root 'install with spaces'
    & $body -InstallDir $install
    & $body -InstallDir $install
    if (-not (Test-Path -LiteralPath (Join-Path $install 'asset-123/rwe.exe'))) { throw 'Engine missing' }
    if ((Get-Content -LiteralPath (Join-Path $install 'rwe.cmd') -Raw) -notmatch '%~dp0asset-123\\rwe.exe" %\*') { throw 'Invalid launcher' }
    $before = Get-Content -LiteralPath (Join-Path $install 'rwe.cmd') -Raw
    $asset.digest = 'sha256:' + ('0' * 64)
    $failed = $false
    try { & $body -InstallDir $install } catch {
        if ($_.Exception.Message -notmatch 'checksum mismatch') { throw }
        $failed = $true
    }
    if (-not $failed) { throw 'Corrupted download was accepted' }
    if ((Get-Content -LiteralPath (Join-Path $install 'rwe.cmd') -Raw) -ne $before) { throw 'Previous launcher changed on failure' }
    if (@(Get-ChildItem -LiteralPath $install -Filter '.install-*' -Force).Count) { throw 'Staging directories leaked' }
    Write-Host 'PASS: Windows install, repeat install, checksum rejection, and failure cleanup'
} finally {
    $resolved = [IO.Path]::GetFullPath($root)
    if ([IO.Path]::GetDirectoryName($resolved).TrimEnd('\') -eq [IO.Path]::GetTempPath().TrimEnd('\') -and
        [IO.Path]::GetFileName($resolved).StartsWith('rwe-test-')) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
