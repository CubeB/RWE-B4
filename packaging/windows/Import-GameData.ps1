# INSTALL-UX-001: graphical presentation of the existing importer; copying,
# source discovery and the data destination remain rwe_setup's responsibility.
param([switch]$NoShow)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()
$script:installDirectory = Split-Path -Parent $PSScriptRoot
$script:importProcess = $null
$script:outputTask = $null
$script:errorTask = $null
$script:importSucceeded = $false

$form = New-Object System.Windows.Forms.Form
$form.Text = 'Robot War Engine - Game Setup'
$form.ClientSize = New-Object System.Drawing.Size(620, 440)
$form.MinimumSize = $form.Size
$form.MaximumSize = $form.Size
$form.StartPosition = 'CenterScreen'
$form.Font = New-Object System.Drawing.Font('Segoe UI', 10)
$form.AutoScaleMode = 'Dpi'
$form.MaximizeBox = $false

function Add-Label($text, $x, $y, $width, $height) {
    $label = New-Object System.Windows.Forms.Label
    $label.Text = $text
    $label.SetBounds($x, $y, $width, $height)
    $form.Controls.Add($label)
    return $label
}

$title = Add-Label 'Bring your Total Annihilation files' 24 22 572 36
$title.Font = New-Object System.Drawing.Font('Segoe UI', 17, [System.Drawing.FontStyle]::Bold)
$intro = Add-Label "RWE needs files from your own copy of Total Annihilation.`r`nSetup copies the game data, movies and music. Your original files stay in place." 24 68 572 54
$folderLabel = Add-Label 'Total Annihilation folder' 24 140 572 24
$source = New-Object System.Windows.Forms.TextBox
$source.Name = 'SourceFolder'
$source.SetBounds(24, 170, 466, 28)
$form.Controls.Add($source)
$browse = New-Object System.Windows.Forms.Button
$browse.Text = 'Browse...'
$browse.SetBounds(500, 168, 96, 32)
$form.Controls.Add($browse)
$hint = Add-Label 'Leave blank to search automatically, or select the folder containing totala1.hpi.' 24 207 572 40
$hint.ForeColor = [System.Drawing.Color]::DimGray
$status = Add-Label 'Ready to import. Existing complete files will be kept.' 24 262 572 48
$status.Name = 'Status'
$progress = New-Object System.Windows.Forms.ProgressBar
$progress.SetBounds(24, 316, 572, 8)
$form.Controls.Add($progress)

$details = New-Object System.Windows.Forms.LinkLabel
$details.Text = 'Show details'
$details.SetBounds(24, 346, 130, 24)
$form.Controls.Add($details)
$log = New-Object System.Windows.Forms.TextBox
$log.Name = 'Details'
$log.Multiline = $true
$log.ReadOnly = $true
$log.ScrollBars = 'Vertical'
$log.SetBounds(24, 386, 572, 160)
$log.Visible = $false
$form.Controls.Add($log)

$import = New-Object System.Windows.Forms.Button
$import.Name = 'Import'
$import.Text = 'Import game files'
$import.SetBounds(286, 386, 180, 34)
$form.Controls.Add($import)
$close = New-Object System.Windows.Forms.Button
$close.Text = 'Set up later'
$close.SetBounds(476, 386, 120, 34)
$form.Controls.Add($close)
$form.AcceptButton = $import
$form.CancelButton = $close

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 200

function Set-DetailsVisible($visible) {
    $log.Visible = $visible
    $details.Text = if ($visible) { 'Hide details' } else { 'Show details' }
    $buttonY = if ($visible) { 566 } else { 386 }
    $form.MaximumSize = New-Object System.Drawing.Size(2000, 2000)
    $form.MinimumSize = New-Object System.Drawing.Size(0, 0)
    $form.ClientSize = New-Object System.Drawing.Size(620, ($buttonY + 54))
    $form.MinimumSize = $form.Size
    $form.MaximumSize = $form.Size
    $import.Top = $buttonY
    $close.Top = $buttonY
}

$details.Add_LinkClicked({ Set-DetailsVisible (-not $log.Visible) })
$browse.Add_Click({
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = 'Choose your Total Annihilation installation (the folder containing totala1.hpi).'
    $dialog.ShowNewFolderButton = $false
    if (Test-Path -LiteralPath $source.Text -PathType Container) { $dialog.SelectedPath = $source.Text }
    if ($dialog.ShowDialog($form) -eq 'OK') { $source.Text = $dialog.SelectedPath }
    $dialog.Dispose()
})
$close.Add_Click({ $form.Close() })

$import.Add_Click({
    if ($script:importSucceeded) {
        try {
            Start-Process -FilePath (Join-Path $script:installDirectory 'rwe.exe') -WorkingDirectory $script:installDirectory
            $form.Close()
        } catch {
            $status.Text = 'Could not start the game. See details, or use the Play shortcut.'
            $status.ForeColor = [System.Drawing.Color]::Firebrick
            $log.Text = $_.Exception.Message
            Set-DetailsVisible $true
        }
        return
    }
    $folder = $source.Text.Trim()
    if ($folder -and -not (Test-Path -LiteralPath $folder -PathType Container)) {
        $status.Text = 'That folder does not exist. Choose your Total Annihilation folder with Browse.'
        $status.ForeColor = [System.Drawing.Color]::Firebrick
        return
    }
    try {
        $arguments = ''
        if ($folder) {
            # Windows filenames cannot contain a quote. Append a path component
            # so a trailing backslash cannot escape the closing argument quote.
            $arguments = '--from "{0}"' -f (Join-Path $folder '.')
        }
        $process = New-Object System.Diagnostics.Process
        $process.StartInfo.FileName = Join-Path $script:installDirectory 'rwe_setup.exe'
        $process.StartInfo.WorkingDirectory = $script:installDirectory
        $process.StartInfo.Arguments = $arguments
        $process.StartInfo.UseShellExecute = $false
        $process.StartInfo.CreateNoWindow = $true
        $process.StartInfo.RedirectStandardOutput = $true
        $process.StartInfo.RedirectStandardError = $true
        $null = $process.Start()
        # Drain both pipes asynchronously; a full stderr pipe must not deadlock
        # a large import. Process.Start also retains a reliable exit-code handle.
        $script:outputTask = $process.StandardOutput.ReadToEndAsync()
        $script:errorTask = $process.StandardError.ReadToEndAsync()
        $script:importProcess = $process
        $status.Text = 'Importing game files. Large installations may take a few minutes...'
        $status.ForeColor = [System.Drawing.SystemColors]::ControlText
        $progress.Style = 'Marquee'
        $source.Enabled = $false
        $browse.Enabled = $false
        $import.Enabled = $false
        $close.Enabled = $false
        $timer.Start()
    } catch {
        $status.Text = 'Could not start the import. See details and try again.'
        $status.ForeColor = [System.Drawing.Color]::Firebrick
        $log.Text = $_.Exception.Message
        Set-DetailsVisible $true
    }
})

$timer.Add_Tick({
    if (-not $script:importProcess -or -not $script:importProcess.HasExited) { return }
    $timer.Stop()
    $script:importProcess.WaitForExit()
    $code = $script:importProcess.ExitCode
    $log.Text = $script:outputTask.Result + "`r`n" + $script:errorTask.Result
    $script:importProcess.Dispose()
    $script:importProcess = $null
    $script:outputTask = $null
    $script:errorTask = $null
    $progress.Style = 'Blocks'
    $import.Enabled = $true
    $close.Enabled = $true
    $close.Text = 'Close'
    if ($code -eq 0) {
        $script:importSucceeded = $true
        $progress.Value = 100
        $status.Text = 'Ready to play! Your game files are installed.'
        $status.ForeColor = [System.Drawing.Color]::DarkGreen
        $import.Text = 'Play now'
    } else {
        $source.Enabled = $true
        $browse.Enabled = $true
        $status.Text = 'Import did not finish. Choose the folder containing totala1.hpi and totala2.hpi, then try again.'
        $status.ForeColor = [System.Drawing.Color]::Firebrick
        Set-DetailsVisible $true
    }
})
$form.Add_FormClosing({
    param($sender, $event)
    # Do not leave an invisible, half-finished copy running after closing.
    if ($script:importProcess -and -not $script:importProcess.HasExited) { $event.Cancel = $true }
})
$form.Add_FormClosed({ $timer.Dispose() })

if (-not $NoShow) { [System.Windows.Forms.Application]::Run($form); $form.Dispose() }
