# DOWNLOAD-002: RWE-original distribution packaging; repack only RWE's own ZIP payload.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $ZipPath,
    [string] $SevenZip,
    [switch] $EngineOnly
)

$ErrorActionPreference = 'Stop'
$zip = Get-Item -LiteralPath $ZipPath -ErrorAction Stop
if ($zip.PSIsContainer -or $zip.Extension -ine '.zip') {
    throw "ZipPath must name an existing .zip file: $ZipPath"
}
if (-not $SevenZip) {
    $sevenZipCommand = Get-Command '7z.exe' -ErrorAction SilentlyContinue
    if ($sevenZipCommand) { $SevenZip = $sevenZipCommand.Source }
    else {
        $candidate = Join-Path $env:ProgramFiles '7-Zip\7z.exe'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $SevenZip = $candidate }
    }
}
if (-not $SevenZip -or -not (Test-Path -LiteralPath $SevenZip -PathType Leaf)) {
    throw '7-Zip is required. Install 7-Zip or pass -SevenZip with the path to 7z.exe.'
}

$suffix = if ($EngineOnly) { '-Engine.7z' } else { '.7z' }
$output = Join-Path $zip.DirectoryName ($zip.BaseName + $suffix)
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$temp = Join-Path $tempRoot ('rwe-portable-' + [guid]::NewGuid().ToString('N'))
$payload = Join-Path $temp 'payload'
$archive = Join-Path $temp 'package.7z'
New-Item -ItemType Directory -Path $payload -Force | Out-Null
try {
    Expand-Archive -LiteralPath $zip.FullName -DestinationPath $payload
    $engine = @(Get-ChildItem -LiteralPath $payload -Filter 'rwe.exe' -File -Recurse)
    $setup = @(Get-ChildItem -LiteralPath $payload -Filter 'rwe_setup.exe' -File -Recurse)
    if ($engine.Count -ne 1 -or $setup.Count -ne 1 -or $engine[0].DirectoryName -ne $setup[0].DirectoryName) {
        throw 'ZIP payload must contain exactly one rwe.exe beside exactly one rwe_setup.exe.'
    }
    $compressionRoot = $payload
    if ($EngineOnly) {
        $sourceRoot = $engine[0].DirectoryName
        $rootName = Split-Path -Leaf $sourceRoot
        if ($sourceRoot -eq $payload) { $rootName = $zip.BaseName }
        $enginePayload = Join-Path $temp 'engine-payload'
        $compressionRoot = Join-Path $enginePayload $rootName
        New-Item -ItemType Directory -Path $compressionRoot -Force | Out-Null
        Get-ChildItem -LiteralPath $sourceRoot -Force | Where-Object {
            $_.Name -ne 'launcher' -and $_.Name -ne 'RWE Launcher.cmd' -and $_.Name -ne 'rwe_bridge.exe'
        } | Copy-Item -Destination $compressionRoot -Recurse -Force
    }
    Push-Location $compressionRoot
    try {
        & $SevenZip a -t7z -mx=9 -ms=on $archive '.\*' -bsp0
        if ($LASTEXITCODE -ne 0) { throw "7-Zip compression failed with exit code $LASTEXITCODE." }
    }
    finally { Pop-Location }
    & $SevenZip t $archive
    if ($LASTEXITCODE -ne 0) { throw "7-Zip archive verification failed with exit code $LASTEXITCODE." }
    Copy-Item -LiteralPath $archive -Destination $output -Force
    $result = Get-Item -LiteralPath $output
    Write-Output ("Created {0} ({1:N0} bytes)" -f $result.FullName, $result.Length)
}
finally {
    $resolvedTempRoot = [IO.Path]::GetFullPath($tempRoot).TrimEnd([IO.Path]::DirectorySeparatorChar)
    $resolvedTemp = [IO.Path]::GetFullPath($temp)
    if ([IO.Path]::GetDirectoryName($resolvedTemp).TrimEnd([IO.Path]::DirectorySeparatorChar) -eq $resolvedTempRoot -and
        [IO.Path]::GetFileName($resolvedTemp) -match '^rwe-portable-[0-9a-f]{32}$') {
        Remove-Item -LiteralPath $resolvedTemp -Recurse -Force
    }
}
