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
#
# Compare two builds by running it against each and diffing the averages.

param(
    [int]$games = 10,
    [int]$seconds = 600,
    [string]$map = "Coast To Coast",
    [string]$difficulty = "standard",
    [string]$sideA = "ARM",
    [string]$sideB = "CORE",
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

    $gameArgs = @(
        '--log', $log,
        '--ai-arena', $seconds,
        '--seed', $seed,
        '--map', ('"' + $map + '"'),
        '--player', ('"A;Computer;' + $sideA + ';0"'),
        '--player', ('"B;Computer;' + $sideB + ';1"'),
        '--ai-difficulty', $difficulty
    )
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
}

$sw.Stop()

if ($results.Count -eq 0) { Write-Error "no games produced a result"; exit 1 }

Write-Host ""
Write-Host ("=== {0} games, {1}s each, {2}, {3} -- {4:n0}s wall ===" -f $results.Count, $seconds, $map, $difficulty, $sw.Elapsed.TotalSeconds)

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
