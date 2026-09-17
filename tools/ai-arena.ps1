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
#   tools\ai-arena.ps1                          # 10 games, 10 minutes each
#   tools\ai-arena.ps1 -games 20 -seconds 900
#   tools\ai-arena.ps1 -difficulty hard -map "Great Divide"
#   tools\ai-arena.ps1 -sideA ARM -sideB ARM -tuneB "attackInWaves=0,holdWhenOutnumbered=0"
#
# Compare two builds by running it against each and diffing the averages.
# Compare two BEHAVIOURS with -tuneA / -tuneB, which set AI knobs for one
# side only (comma-separated knob=value, names as in AiTuningProfile.h): a
# mirror match cannot show whether a change helped, because both sides get
# it, so play the change against its absence in the same game -- and give
# both sides the same faction, or the faction difference is measured too.
#
# Seats are DEALT by default (-startLocation random), and that is not a
# cosmetic choice. The engine defaults to fixed start positions, meaning
# player 0 always takes the map's StartPos 0 and player 1 StartPos 1, on
# every seed -- so before this was passed, every table this script printed
# compared one seat against another seat rather than one behaviour against
# another. On Hundred Isles those two seats differ by more than any AI knob
# measured so far (player 0 averaged 16.9 buildings and 12.5 metal a second
# and died once in ten games; player 1 averaged 4.6 and 6.6 and died seven
# times in ten -- with no tune on either side), and it produced a false
# positive that only a control run caught. Pass -startLocation fixed to get
# the old behaviour back, and if you do, run the control.

param(
    [int]$games = 10,
    [int]$seconds = 1800,   # a cap, not the length: a game ends when somebody wins
    [string]$map = "Coast To Coast",
    [string]$difficulty = "standard",
    [string]$startLocation = "random",
    [string]$sideA = "ARM",
    [string]$sideB = "CORE",
    [string]$tuneA = "",
    [string]$tuneB = "",
    [string]$exe = "D:\RWE\build-release\rwe.exe",
    [string]$outDir = "$env:TEMP\rwe-arena"
)

if (-not (Test-Path $exe)) { Write-Error "No such executable: $exe"; exit 1 }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$results = @()
$sw = [System.Diagnostics.Stopwatch]::StartNew()

for ($seed = 1; $seed -le $games; $seed++) {
    $log = Join-Path $outDir "game-$seed.log"
    if (Test-Path $log) { Remove-Item $log }

    # Every game keeps its replay in the Replays folder, where replay_viewer
    # lists it, so anything the averages flag as odd can then be watched.
    $replay = "arena-$seed"
    $gameArgs = @(
        '--log', $log,
        '--ai-arena', $seconds,
        '--seed', $seed,
        '--record-replay', $replay,
        '--map', ('"' + $map + '"'),
        '--player', ('"A;Computer;' + $sideA + ';0"'),
        '--player', ('"B;Computer;' + $sideB + ';1"'),
        '--ai-difficulty', $difficulty,
        '--start-location', $startLocation
    )
    foreach ($t in ($tuneA -split ',' | Where-Object { $_ })) { $gameArgs += @('--ai-tune', ('0:' + $t)) }
    foreach ($t in ($tuneB -split ',' | Where-Object { $_ })) { $gameArgs += @('--ai-tune', ('1:' + $t)) }
    $p = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -ArgumentList $gameArgs -PassThru -Wait
    if ($p.ExitCode -ne 0) { Write-Warning "game $seed exited $($p.ExitCode)"; continue }

    $line = Select-String -Path $log -Pattern 'AI-ARENA-RESULT' -SimpleMatch | Select-Object -Last 1
    if (-not $line) { Write-Warning "game $seed produced no result line"; continue }

    # p0=ARM alive units=24 buildings=19 army=1 lost=1 metalIncome=6
    $text = $line.Line
    $row = [ordered]@{ seed = $seed }
    foreach ($side in @(0, 1)) {
        if ($text -match "p$side=(\w+) (\w+) units=(\d+) buildings=(\d+) army=(\d+) lost=(\d+) metalIncome=(-?\d+)") {
            $row["p$side" + "_side"] = $Matches[1]
            $row["p$side" + "_status"] = $Matches[2]
            $row["p$side" + "_units"] = [int]$Matches[3]
            $row["p$side" + "_buildings"] = [int]$Matches[4]
            $row["p$side" + "_army"] = [int]$Matches[5]
            $row["p$side" + "_lost"] = [int]$Matches[6]
            $row["p$side" + "_metal"] = [int]$Matches[7]
        }
    }
    $results += [pscustomobject]$row
    Write-Host ("game {0,-3} {1}" -f $seed, ($text -replace '.*AI-ARENA-RESULT ', ''))

    # The economy samples and events are written to one fixed pair of files
    # in the data folder, so each game overwrites the last. Keep a copy per
    # game: a stall that shows in the averages needs the run it came from.
    foreach ($name in @('ai-arena.csv', 'ai-arena-events.csv')) {
        $src = Join-Path $env:APPDATA ("RWE/" + $name)
        if (Test-Path $src) { Copy-Item $src (Join-Path $outDir ("game-$seed-" + $name)) -Force }
    }
}

$sw.Stop()

if ($results.Count -eq 0) { Write-Error "no games produced a result"; exit 1 }

Write-Host ""
Write-Host ("=== {0} games, {1}s each, {2}, {3}, seats {4} -- {5:n0}s wall ===" -f $results.Count, $seconds, $map, $difficulty, $startLocation, $sw.Elapsed.TotalSeconds)

# "Alive at the cap" is not a win on its own: at ten minutes both sides
# usually still stand. What separates them is what they built and kept, so
# report the averages and let the reader see which way they moved.
foreach ($side in @(0, 1)) {
    $name = $results[0].("p$side" + "_side")
    $avg = [pscustomobject]@{
        player    = "p$side ($name)"
        units     = [math]::Round((($results | Measure-Object -Property ("p$side" + "_units") -Average).Average), 1)
        buildings = [math]::Round((($results | Measure-Object -Property ("p$side" + "_buildings") -Average).Average), 1)
        army      = [math]::Round((($results | Measure-Object -Property ("p$side" + "_army") -Average).Average), 1)
        lost      = [math]::Round((($results | Measure-Object -Property ("p$side" + "_lost") -Average).Average), 1)
        metalRate = [math]::Round((($results | Measure-Object -Property ("p$side" + "_metal") -Average).Average), 1)
        dead      = ($results | Where-Object { $_."p$side`_status" -eq 'dead' }).Count
    }
    $avg | Format-Table -AutoSize | Out-String | Write-Host
}

$results | Export-Csv -NoTypeInformation -Path (Join-Path $outDir "summary.csv")
Write-Host ("per-game rows: " + (Join-Path $outDir "summary.csv"))
Write-Host ("replays:       arena-1..arena-$games in the Replays folder  (watch with: replay_viewer)")

# The last game's own page, because averages say whether a change helped and
# a timeline says what the AI actually did. A local file: nothing to sign in to.
$python = "D:/msys64/mingw64/bin/python.exe"
$csv = Join-Path $env:APPDATA "RWE/ai-arena.csv"
if ((Test-Path $python) -and (Test-Path $csv)) {
    $html = Join-Path $env:APPDATA "RWE/ai-arena.html"
    & $python "D:/RWE/tools/arena-report.py" $csv --out $html | Out-Null
    if (Test-Path $html) { Write-Host ("last game, as a page: " + $html) }
}
