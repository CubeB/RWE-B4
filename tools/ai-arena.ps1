# Runs a batch of computer-versus-computer games and reduces them to a table.
#
# This exists because reasoning about AI changes has been wrong before: gating
# the whole metal search on exploration looked obviously right and measurably
# starved the opening, and two runs caught what thinking about it did not. A
# change to the AI should be judged by playing it.
#
# Each game runs headless at roughly a hundred times real time, so twenty
# ten-minute games take about two minutes.
#
#   tools\ai-arena.ps1                                  # 10 games, screening
#   tools\ai-arena.ps1 -tune "targetShipyardCount=2"    # a change, self-controlled
#   tools\ai-arena.ps1 -tune "..." -confirm             # screen, then confirm long
#   tools\ai-arena.ps1 -difficulty hard -map "Great Divide"
#
# -tune IS THE ONE TO USE. It names the change under test and then does three
# things that took a whole day to learn were necessary:
#
#   It runs its own control. Every seed is played twice, once with the change
#   and once with no tune anywhere, so the table shows the change against its
#   absence and not merely one player against another. The day this was added,
#   a knob had already produced a table that looked like a rout -- triple the
#   buildings, double the income, the other side dead in six games of ten --
#   and the control reproduced the whole of it with no knob anywhere. The
#   cost of the control arm is one extra batch; the cost of not having it was
#   a false positive believed for an hour.
#
#   It alternates which player gets the change. Odd seeds give it to player 0
#   and even seeds to player 1, so a seat that happens to be stronger is
#   shared equally between the arms instead of being confounded with the
#   change. Dealing the seats at random does this too, eventually: measured,
#   ten dealt games came out seven-three rather than five-five, which is
#   exactly the variance that forced twenty-game runs. Alternation is free and
#   halves it.
#
#   It reports arms, not players. With the change alternating, a p0-versus-p1
#   table means nothing; "tuned" and "untuned" are the rows that matter, and
#   the control arm is printed per player underneath, because that is where a
#   seat imbalance would still show up.
#
# -tuneA / -tuneB are the older, blunter form: they pin a change to one player
# and run no control. They are kept for the case where the two sides really
# are meant to differ, and they are not what you want for measuring a change.
#
# SCREEN SHORT, CONFIRM LONG. The default is 900 seconds because build-order
# effects -- which is most of what the AI knobs move -- have all shown up
# inside the first ten minutes, and most games reach the cap anyway. Use
# -confirm to follow a screening run with a long one at -confirmSeconds.
#
# Seats are DEALT by default (-startLocation random). The engine defaults to
# fixed start positions, meaning player 0 always takes the map's StartPos 0
# and player 1 StartPos 1, on every seed -- so before this was passed, every
# table this script printed compared one seat against another seat rather than
# one behaviour against another. On Hundred Isles those two seats differ by
# more than any AI knob measured so far. Pass -startLocation fixed to get the
# old behaviour back, and if you do, run the control.

param(
    [int]$games = 10,
    [int]$seconds = 900,          # screening length; a cap, not the length
    [int]$confirmSeconds = 1800,  # used by -confirm
    [switch]$confirm,
    [string]$map = "Coast To Coast",
    [string]$difficulty = "standard",
    [string]$startLocation = "random",
    [string]$sideA = "ARM",
    [string]$sideB = "CORE",
    [string]$tune = "",           # the change under test: alternates sides, self-controls
    [string]$tuneA = "",          # legacy: pin a change to player 0, no control
    [string]$tuneB = "",          # legacy: pin a change to player 1, no control
    [switch]$noControl,           # skip the control arm when only the A/B is wanted
    [string]$countTypes = "",     # comma-separated unit types to add as columns
    [string]$exe = "D:\RWE\build-release\rwe.exe",
    [string]$outDir = "$env:TEMP\rwe-arena"
)

if (-not (Test-Path $exe)) { Write-Error "No such executable: $exe"; exit 1 }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

if ($tune -and ($tuneA -or $tuneB)) {
    Write-Error "-tune alternates sides and self-controls; -tuneA/-tuneB pin a side. Use one form or the other."
    exit 1
}
if ($tune -and ($sideA -ne $sideB)) {
    Write-Warning "-tune with different factions measures the faction difference too. Set -sideA and -sideB the same."
}

$typeList = @($countTypes -split ',' | Where-Object { $_ } | ForEach-Object { $_.Trim() })

# One game. tunedPlayer is 0 or 1 for the -tune form, -1 for a control game or
# when the legacy -tuneA/-tuneB are doing the work.
function Invoke-ArenaGame {
    param(
        [int]$seed,
        [int]$secs,
        [int]$tunedPlayer,
        [string]$arm
    )

    $tag = "game-$seed"
    if ($arm -eq 'control') { $tag = "game-$seed-control" }
    $log = Join-Path $outDir "$tag.log"
    if (Test-Path $log) { Remove-Item $log }

    # Every game keeps its replay in the Replays folder, where replay_viewer
    # lists it, so anything the averages flag as odd can then be watched.
    $gameArgs = @(
        '--log', $log,
        '--ai-arena', $secs,
        '--seed', $seed,
        '--record-replay', "arena-$tag",
        '--map', ('"' + $map + '"'),
        '--player', ('"A;Computer;' + $sideA + ';0"'),
        '--player', ('"B;Computer;' + $sideB + ';1"'),
        '--ai-difficulty', $difficulty,
        '--start-location', $startLocation
    )

    if ($tunedPlayer -ge 0) {
        foreach ($t in ($tune -split ',' | Where-Object { $_ })) {
            $gameArgs += @('--ai-tune', ("$tunedPlayer" + ':' + $t))
        }
    }
    foreach ($t in ($tuneA -split ',' | Where-Object { $_ })) { $gameArgs += @('--ai-tune', ('0:' + $t)) }
    foreach ($t in ($tuneB -split ',' | Where-Object { $_ })) { $gameArgs += @('--ai-tune', ('1:' + $t)) }

    $p = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -ArgumentList $gameArgs -PassThru -Wait
    if ($p.ExitCode -ne 0) { Write-Warning "$tag exited $($p.ExitCode)"; return $null }

    $line = Select-String -Path $log -Pattern 'AI-ARENA-RESULT' -SimpleMatch | Select-Object -Last 1
    if (-not $line) { Write-Warning "$tag produced no result line"; return $null }

    $text = $line.Line
    $row = [ordered]@{ seed = $seed; arm = $arm; tunedPlayer = $tunedPlayer }
    foreach ($side in @(0, 1)) {
        # types= is appended last by AiArenaReport and is optional, so an older
        # binary still parses here rather than dropping the whole row.
        if ($text -match "p$side=(\w+) (\w+) units=(\d+) buildings=(\d+) army=(\d+) lost=(\d+) metalIncome=(-?\d+)") {
            $row["p$side" + "_side"] = $Matches[1]
            $row["p$side" + "_status"] = $Matches[2]
            $row["p$side" + "_units"] = [int]$Matches[3]
            $row["p$side" + "_buildings"] = [int]$Matches[4]
            $row["p$side" + "_army"] = [int]$Matches[5]
            $row["p$side" + "_lost"] = [int]$Matches[6]
            $row["p$side" + "_metal"] = [int]$Matches[7]
        }
        $counts = @{}
        if ($text -match "p$side=.*? types=(\S+)") {
            foreach ($piece in ($Matches[1] -split ';' | Where-Object { $_ -and $_ -ne '-' })) {
                if ($piece -match '^(.*)x(\d+)$') { $counts[$Matches[1]] = [int]$Matches[2] }
            }
        }
        $row["p$side" + "_types"] = $counts
    }
    $row['text'] = $text
    return [pscustomobject]$row
}

# Pulls one player's figures out of a row, so an arm can be assembled from
# whichever player happened to be carrying the change that seed.
function Get-Side {
    param($row, [int]$side)
    $t = $row."p$side`_types"
    if (-not $t) { $t = @{} }
    return [pscustomobject]@{
        units     = $row."p$side`_units"
        buildings = $row."p$side`_buildings"
        army      = $row."p$side`_army"
        lost      = $row."p$side`_lost"
        metal     = $row."p$side`_metal"
        dead      = ($row."p$side`_status" -eq 'dead')
        types     = $t
    }
}

function Show-Arm {
    param([string]$name, $samples)
    if (-not $samples -or $samples.Count -eq 0) { return }
    $avg = [ordered]@{
        arm       = $name
        games     = $samples.Count
        units     = [math]::Round((($samples | Measure-Object -Property units -Average).Average), 1)
        buildings = [math]::Round((($samples | Measure-Object -Property buildings -Average).Average), 1)
        army      = [math]::Round((($samples | Measure-Object -Property army -Average).Average), 1)
        lost      = [math]::Round((($samples | Measure-Object -Property lost -Average).Average), 1)
        metalRate = [math]::Round((($samples | Measure-Object -Property metal -Average).Average), 1)
        dead      = ($samples | Where-Object { $_.dead }).Count
    }
    foreach ($t in $typeList) {
        $total = 0
        foreach ($s in $samples) { if ($s.types.ContainsKey($t)) { $total += $s.types[$t] } }
        $avg[$t] = [math]::Round(($total / $samples.Count), 2)
    }
    return [pscustomobject]$avg
}

function Invoke-Phase {
    param([string]$phase, [int]$secs)

    $rows = @()
    $sw = [System.Diagnostics.Stopwatch]::StartNew()

    for ($seed = 1; $seed -le $games; $seed++) {
        if ($tune) {
            # Odd seeds give the change to player 0, even seeds to player 1, so
            # a stronger seat is shared between the arms rather than confounded
            # with the change.
            $tunedPlayer = 1 - ($seed % 2)
            $r = Invoke-ArenaGame -seed $seed -secs $secs -tunedPlayer $tunedPlayer -arm 'tuned'
            if ($r) {
                $rows += $r
                Write-Host ("[{0}] game {1,-3} tuned=p{2}  {3}" -f $phase, $seed, $tunedPlayer, ($r.text -replace '.*AI-ARENA-RESULT ', ''))
            }
            if (-not $noControl) {
                $c = Invoke-ArenaGame -seed $seed -secs $secs -tunedPlayer -1 -arm 'control'
                if ($c) {
                    $rows += $c
                    Write-Host ("[{0}] game {1,-3} control    {2}" -f $phase, $seed, ($c.text -replace '.*AI-ARENA-RESULT ', ''))
                }
            }
        }
        else {
            $r = Invoke-ArenaGame -seed $seed -secs $secs -tunedPlayer -1 -arm 'plain'
            if ($r) {
                $rows += $r
                Write-Host ("[{0}] game {1,-3} {2}" -f $phase, $seed, ($r.text -replace '.*AI-ARENA-RESULT ', ''))
            }
        }

        # The economy samples and events are written to one fixed pair of files
        # in the data folder, so each game overwrites the last. Keep a copy per
        # game: a stall that shows in the averages needs the run it came from.
        foreach ($name in @('ai-arena.csv', 'ai-arena-events.csv')) {
            $src = Join-Path $env:APPDATA ("RWE/" + $name)
            if (Test-Path $src) { Copy-Item $src (Join-Path $outDir ("game-$seed-" + $name)) -Force }
        }
    }

    $sw.Stop()
    if ($rows.Count -eq 0) { Write-Error "no games produced a result"; return $null }

    Write-Host ""
    Write-Host ("=== {0}: {1} games, {2}s each, {3}, {4}, seats {5} -- {6:n0}s wall ===" -f `
        $phase, $games, $secs, $map, $difficulty, $startLocation, $sw.Elapsed.TotalSeconds)
    Write-Host ""

    $table = @()
    if ($tune) {
        $tunedRows = @($rows | Where-Object { $_.arm -eq 'tuned' })
        $tunedSamples = @($tunedRows | ForEach-Object { Get-Side $_ $_.tunedPlayer })
        $untunedSamples = @($tunedRows | ForEach-Object { Get-Side $_ (1 - $_.tunedPlayer) })
        $table += Show-Arm "tuned" $tunedSamples
        $table += Show-Arm "untuned (same games)" $untunedSamples

        $controlRows = @($rows | Where-Object { $_.arm -eq 'control' })
        if ($controlRows.Count -gt 0) {
            # Per player, deliberately: this is the row where a seat imbalance
            # shows itself, and folding it away would hide the very thing the
            # control arm is here to expose.
            $table += Show-Arm "control p0" @($controlRows | ForEach-Object { Get-Side $_ 0 })
            $table += Show-Arm "control p1" @($controlRows | ForEach-Object { Get-Side $_ 1 })
        }
    }
    else {
        $table += Show-Arm "p0" @($rows | ForEach-Object { Get-Side $_ 0 })
        $table += Show-Arm "p1" @($rows | ForEach-Object { Get-Side $_ 1 })
    }

    # Rendered by hand, because Format-Table cannot be trusted with this.
    # Asked for three type columns it printed two and said nothing about the
    # third, and the reason is not the one you would guess: the host is 120
    # columns wide, the table needs about 66, and Out-String -Width 4096 makes
    # no difference. Windows PowerShell's Format-Table takes only the first ten
    # properties off a PSCustomObject and silently drops the rest -- so the
    # eleventh column disappears however much room there is. Eight fixed
    # columns plus -countTypes means that ceiling is reached by the third type
    # asked for. A metrics table that silently discards a metric is the same
    # fault as a log extraction that silently truncates, which is the mistake
    # this whole change exists to stop making. Padding by hand cannot drop one.
    if ($table.Count -gt 0) {
        $cols = @($table[0].PSObject.Properties.Name)
        $width = @{}
        foreach ($c in $cols) {
            $len = $c.Length
            foreach ($r in $table) {
                $v = [string]$r.$c
                if ($v.Length -gt $len) { $len = $v.Length }
            }
            $width[$c] = $len
        }
        # Built once, then shown AND saved -- because the two obvious ways of
        # printing it each fail one of those. Write-Host goes straight to the
        # console and bypasses the output stream, so the table cannot be
        # redirected, tee'd or diffed, which is useless for a tool whose whole
        # purpose is comparing one measurement against another; every
        # Tee-Object of this script had been capturing everything except the
        # results. Write-Output instead makes the lines part of this function's
        # RETURN VALUE, where they are swallowed by the caller that wants the
        # rows -- which produced no table at all, in either phase.
        #
        # So: write to the host for the reader, and to a file for the diff, and
        # return only the rows.
        $lines = @()
        $lines += (($cols | ForEach-Object { $_.PadRight($width[$_]) }) -join '  ')
        $lines += (($cols | ForEach-Object { '-' * $width[$_] }) -join '  ')
        foreach ($r in $table) {
            $lines += (($cols | ForEach-Object { ([string]$r.$_).PadRight($width[$_]) }) -join '  ')
        }
        foreach ($l in $lines) { Write-Host $l }
        Write-Host ""
        $tablePath = Join-Path $outDir "arm-table-$phase.txt"
        Set-Content -Path $tablePath -Value $lines -Encoding utf8
        Write-Host ("arm table:     " + $tablePath)
    }

    $flat = $rows | ForEach-Object {
        $o = [ordered]@{ seed = $_.seed; arm = $_.arm; tunedPlayer = $_.tunedPlayer }
        foreach ($side in @(0, 1)) {
            foreach ($f in @('side', 'status', 'units', 'buildings', 'army', 'lost', 'metal')) {
                $o["p$side`_$f"] = $_."p$side`_$f"
            }
            $t = $_."p$side`_types"
            $pieces = @()
            if ($t) { foreach ($k in ($t.Keys | Sort-Object)) { $pieces += ("$k" + "x" + $t[$k]) } }
            $o["p$side`_types"] = ($pieces -join ';')
        }
        [pscustomobject]$o
    }
    $csv = Join-Path $outDir "summary-$phase.csv"
    $flat | Export-Csv -NoTypeInformation -Path $csv
    Write-Host ("per-game rows: " + $csv)
    return $rows
}

$screen = Invoke-Phase -phase "screen" -secs $seconds
if ($confirm -and $screen) {
    Write-Host ""
    Write-Host "--- screening done; confirming at $confirmSeconds s ---"
    Invoke-Phase -phase "confirm" -secs $confirmSeconds | Out-Null
}

Write-Host ("replays:       arena-game-1..arena-game-$games in the Replays folder  (watch with: replay_viewer)")

# The last game's own page, because averages say whether a change helped and
# a timeline says what the AI actually did. A local file: nothing to sign in to.
$python = "D:/msys64/mingw64/bin/python.exe"
$csvPath = Join-Path $env:APPDATA "RWE/ai-arena.csv"
if ((Test-Path $python) -and (Test-Path $csvPath)) {
    $html = Join-Path $env:APPDATA "RWE/ai-arena.html"
    & $python "D:/RWE/tools/arena-report.py" $csvPath --out $html | Out-Null
    if (Test-Path $html) { Write-Host ("last game, as a page: " + $html) }
}
