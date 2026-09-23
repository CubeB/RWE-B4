# net-test.ps1 -- a network game on one machine, for testing the lockstep path.
#
# Starts N peers of the same game on loopback, each with its own port, log and
# sync hash log, and pushes every window off the side of the screen so the run
# does not take over the desktop. Then it can kill one of them, and it always
# compares what the survivors simulated.
#
# The comparison is the point. Every peer writes RWE_HASH_LOG, one line a tick,
# so two peers that stayed in step have identical files and two that did not
# have a first differing line -- which is the tick, and the same tick the game's
# own desync report names. A run that ends "peers agree" has been checked
# against the only thing that can check it.
#
#   tools/net-test.ps1                         # 2 peers, 60s, nobody killed
#   tools/net-test.ps1 -peers 3 -kill 2        # 3 peers, kill player 2 midway
#   tools/net-test.ps1 -ai                     # add a computer player at the end
#   tools/net-test.ps1 -desyncAt 200           # peer 0 reports a wrong hash from
#                                              # tick 200, to fire the report
#   tools/net-test.ps1 -chat                   # every peer says one line, to
#                                              # show chat crossing the wire
#
# Two things it is good for beyond drop handling: any change to the simulation
# can be run past it to see whether two peers still agree, and RWE_DESYNC_AT
# gives the desync report something to report without waiting for a real fault.
#
# It needs a display -- `rwe --ai-arena` still brings up a GL context (#156) --
# so this is a local tool and not a CI one.
param(
    [int]$peers = 2,
    [int]$seconds = 60,
    [int]$kill = -1,
    [int]$killAfter = 20,
    [switch]$ai,
    [int]$desyncAt = 0,
    [switch]$chat,
    [string]$map = "Coast To Coast",
    [int]$basePort = 15337,
    [string]$exe = "D:\RWE\build-release\rwe.exe",
    [string]$outDir = "$env:TEMP\rwe-net-test"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $exe)) { throw "No engine at $exe. Build the rwe target first." }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
Remove-Item "$outDir\*.log", "$outDir\*.hashes" -ErrorAction SilentlyContinue

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class NetTestWin {
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr p);
}
"@ -ErrorAction SilentlyContinue

# A genuinely minimised window stops being drawn and the game stops ticking at
# full rate, so the window is moved off the side instead -- the same trick
# visual-test.ps1 uses, and for the same reason.
function Move-Offscreen([int]$procId) {
    $script:found = [IntPtr]::Zero
    $cb = [NetTestWin+EnumWindowsProc]{
        param($h, $p)
        $q = 0
        [NetTestWin]::GetWindowThreadProcessId($h, [ref]$q) | Out-Null
        if ($q -eq $procId -and [NetTestWin]::IsWindowVisible($h)) { $script:found = $h; return $false }
        return $true
    }
    [NetTestWin]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
    if ($script:found -ne [IntPtr]::Zero) {
        # SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
        [NetTestWin]::SetWindowPos($script:found, [IntPtr]::Zero, -32000, -32000, 0, 0, (0x1 -bor 0x4 -bor 0x10)) | Out-Null
        return $true
    }
    return $false
}

$names = @("Alice", "Bob", "Carol", "Dave", "Erin", "Frank", "Grace", "Heidi")
$sides = @("ARM", "CORE")

$logs = @(); $hashes = @(); $procs = @()
for ($me = 0; $me -lt $peers; $me++) {
    $logs += "$outDir\peer$me.log"
    $hashes += "$outDir\peer$me.hashes"
}

for ($me = 0; $me -lt $peers; $me++) {
    $a = "--map `"$map`" --width 320 --height 240 --seed 7 --port $($basePort + $me)"
    for ($p = 0; $p -lt $peers; $p++) {
        $side = $sides[$p % 2]
        if ($p -eq $me) { $a += " --player `"$($names[$p]);Human;$side;$p`"" }
        else            { $a += " --player `"$($names[$p]);Network,[::1]:$($basePort + $p);$side;$p`"" }
    }
    if ($ai) { $a += " --player `"Computer;Computer;$($sides[$peers % 2]);$peers`"" }
    $a += " --log `"$($logs[$me])`""

    # Only one peer may counterfeit a desync: the report exists to show two
    # peers disagreeing, and both lying would be two peers agreeing again.
    if ($desyncAt -gt 0 -and $me -eq 0) { $env:RWE_DESYNC_AT = "$desyncAt" }

    # Each peer says one line, at a tick of its own so the order is known:
    # nobody is at these keyboards, and a line that arrives at every other
    # peer is the whole of what chat has to do.
    if ($chat) { $env:RWE_CHAT_TEST = "$(300 + (60 * $me)):hello from $($names[$me])" }

    $env:RWE_HASH_LOG = $hashes[$me]
    $procs += Start-Process -FilePath $exe -ArgumentList $a -PassThru
    Remove-Item Env:\RWE_DESYNC_AT -ErrorAction SilentlyContinue
    Remove-Item Env:\RWE_CHAT_TEST -ErrorAction SilentlyContinue
}
Remove-Item Env:\RWE_HASH_LOG -ErrorAction SilentlyContinue

Write-Output "$peers peers, pids $($procs.Id -join ', '), logs in $outDir"

for ($i = 0; $i -lt 80; $i++) {
    Start-Sleep -Milliseconds 250
    $all = $true
    foreach ($p in $procs) { if (-not (Move-Offscreen $p.Id)) { $all = $false } }
    if ($all) { break }
}

if ($kill -ge 0) {
    Start-Sleep -Seconds $killAfter
    Write-Output "killing player $kill"
    Stop-Process -Id $procs[$kill].Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds ($seconds - $killAfter)
} else {
    Start-Sleep -Seconds $seconds
}

$survivors = 0..($peers - 1) | Where-Object { $_ -ne $kill }
foreach ($s in $survivors) {
    $last = Get-Content $hashes[$s] -ErrorAction SilentlyContinue | Select-Object -Last 1
    $exited = $procs[$s].HasExited
    Write-Output "player $s : last hash line '$last'$(if ($exited) { '  (EXITED ON ITS OWN)' })"
}

# Every survivor against the first survivor, tick for tick.
$reference = @(Get-Content $hashes[$survivors[0]] -ErrorAction SilentlyContinue)
$agreed = $true
foreach ($s in $survivors | Select-Object -Skip 1) {
    $other = @(Get-Content $hashes[$s] -ErrorAction SilentlyContinue)
    $n = [Math]::Min($reference.Count, $other.Count)
    $firstDiff = 0
    for ($i = 0; $i -lt $n; $i++) { if ($reference[$i] -ne $other[$i]) { $firstDiff = $i + 1; break } }
    if ($firstDiff -gt 0) {
        $agreed = $false
        Write-Output "DIVERGED: player $($survivors[0]) and player $s differ from hash line $firstDiff"
        Write-Output "  $($survivors[0]): $($reference[$firstDiff-1])"
        Write-Output "  $s`: $($other[$firstDiff-1])"
    } else {
        Write-Output "player $($survivors[0]) and player $s agree on all $n compared ticks"
    }
}
if ($agreed -and $survivors.Count -gt 1) { Write-Output "RESULT: peers agree" }

foreach ($p in $procs) { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } }

foreach ($s in 0..($peers - 1)) {
    $interesting = Get-Content $logs[$s] -ErrorAction SilentlyContinue |
        Select-String -Pattern "waiting for|quiet for|dropped|left the game|Desync|diverged at tick|critical|Chat from player"
    if ($interesting) {
        Write-Output "=================== player $s ==================="
        $interesting | Select-Object -First 10
    }
}
