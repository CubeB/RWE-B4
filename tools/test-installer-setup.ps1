# Exercise the real importer through its graphical front end without touching
# the user's data. Run with Windows PowerShell -STA and a built rwe_setup.exe.
param([string]$Importer = "$PSScriptRoot/../build-release/rwe_setup.exe")
$ErrorActionPreference = 'Stop'
$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('rwe-setup-' + [guid]::NewGuid())
$savedAppData = $env:APPDATA
try {
    $package = New-Item -ItemType Directory -Path "$testRoot/Package With Spaces/setup" -Force
    $fixture = New-Item -ItemType Directory -Path "$testRoot/Source With Spaces" -Force
    New-Item -ItemType Directory -Path "$testRoot/Invalid Install" -Force | Out-Null
    Copy-Item -LiteralPath "$PSScriptRoot/../packaging/windows/Import-GameData.ps1" -Destination $package.FullName
    Copy-Item -LiteralPath $Importer -Destination "$testRoot/Package With Spaces/rwe_setup.exe"
    foreach ($name in @('totala1.hpi', 'totala2.hpi')) {
        [System.IO.File]::WriteAllText((Join-Path $fixture.FullName $name), 'import fixture')
    }
    $env:APPDATA = "$testRoot/User Data"
    . "$testRoot/Package With Spaces/setup/Import-GameData.ps1" -NoShow
    $form.ShowInTaskbar = $false
    $form.StartPosition = 'Manual'
    $form.Location = New-Object System.Drawing.Point(-2000, -2000)
    $form.Show()

    # A real process failure must not be mistaken for successful setup.
    $source.Text = "$testRoot/Invalid Install"
    $import.PerformClick()
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while ($script:importProcess -and [DateTime]::UtcNow -lt $deadline) {
        [System.Windows.Forms.Application]::DoEvents()
        Start-Sleep -Milliseconds 25
    }
    if ($script:importProcess -or $script:importSucceeded -or -not $import.Enabled -or -not $log.Visible) {
        throw 'Failed imports must show details and allow retry.'
    }

    # Retry with spaces and a trailing separator; verify actual files on disk.
    $source.Text = $fixture.FullName + '\'
    $import.PerformClick()
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while ($script:importProcess -and [DateTime]::UtcNow -lt $deadline) {
        [System.Windows.Forms.Application]::DoEvents()
        Start-Sleep -Milliseconds 25
    }
    if (-not $script:importSucceeded -or $import.Text -ne 'Play now') {
        throw "Successful import was not reported: $($log.Text)"
    }
    foreach ($name in @('totala1.hpi', 'totala2.hpi')) {
        $copied = [System.IO.File]::ReadAllText("$env:APPDATA/RWE/Data/$name")
        if ($copied -ne 'import fixture') { throw "Wrong copied contents: $name" }
    }
    Write-Output 'PASS: process failure, details, retry, spaced paths, trailing slash, actual copy, Play now.'
} finally {
    if ($script:importProcess) {
        if (-not $script:importProcess.HasExited) { $script:importProcess.Kill(); $script:importProcess.WaitForExit() }
        $script:importProcess.Dispose()
        $script:importProcess = $null
    }
    if ($form) { $form.Close(); $form.Dispose() }
    $env:APPDATA = $savedAppData
    # Only remove the unique temporary directory created above.
    $resolvedTestRoot = [System.IO.Path]::GetFullPath($testRoot)
    $resolvedTempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    if ($resolvedTestRoot.StartsWith($resolvedTempRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}
