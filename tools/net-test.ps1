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
#   tools/net-test.ps1 -rejoin                 # kill a peer, then bring it back
#                                              # and check it is still in step
#   tools/net-test.ps1 -rejoin -bridge         # the same, asked for the way a
#                                              # launcher asks: over the game's
#                                              # own stdin and stdout
#   tools/net-test.ps1 -lag 1:50               # peer 1 sleeps 50 ms after every
#                                              # tick, to reproduce a slow machine
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
    [string]$lag = "",
    [switch]$rejoin,
    [switch]$bridge,
    [int]$rejoinAfter = 8,
    [string]$map = "Coast To Coast",
    [int]$basePort = 15337,
    [string]$exe = "D:\RWE\build-release\rwe.exe",
    [string]$outDir = "$env:TEMP\rwe-net-test"
)

$ErrorActionPreference = "Stop"

# A rejoin needs somebody to have left. Killing the highest-numbered peer keeps
# player 0 alive, which is the peer entitled to declare both the drop and the
# return.
if ($rejoin -and $kill -lt 0) { $kill = $peers - 1 }

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

# One line of the game's own output, or nothing if it has not said anything
# within the time allowed. Async because a blocking read would hang the harness
# for good on a game that has stopped saying anything.
function Read-BridgeLine([int]$timeoutMs) {
    # The outstanding read is kept rather than started afresh: a read that has
    # not finished still owns the stream, and asking for a second one throws
    # rather than waiting.
    if (-not $script:bridgeRead) {
        $script:bridgeRead = $script:bridgeProc.StandardOutput.ReadLineAsync()
    }
    if ($script:bridgeRead.Wait($timeoutMs)) {
        $line = $script:bridgeRead.Result
        $script:bridgeRead = $null
        return $line
    }
    return $null
}

# Reads until an event of this name arrives, and hands back its fields.
function Wait-BridgeEvent([string]$name, [int]$timeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($timeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $line = Read-BridgeLine 1000
        if (-not $line) { continue }
        try { $j = $line | ConvertFrom-Json } catch { continue }
        # To the console and not down the pipeline: anything written out of a
        # function is part of what that function returns, and this one returns
        # the event.
        Write-Host "bridge said: $line"
        if ($j.event -eq $name) { return $j }
    }
    return $null
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

    # Only a peer recording a replay can hand a returning one the ticks it
    # missed, nothing else keeping the commands. Peer 0 is the one that will be
    # asked for it, being the lowest-numbered peer and so the one entitled to
    # declare both the drop and the rejoin.
    if ($rejoin) { $a += " --record-replay `"$outDir\peer$me.rwereplay`"" }

    # Ask for the killed peer back, a few seconds after it has been declared
    # lost. Only the peer entitled to say so acts on it; the rest ignore it.
    if ($rejoin -and $me -ne $kill -and -not $bridge) { $env:RWE_REJOIN_TEST = "$kill`:$rejoinAfter" }

    # Only one peer may counterfeit a desync: the report exists to show two
    # peers disagreeing, and both lying would be two peers agreeing again.
    if ($desyncAt -gt 0 -and $me -eq 0) { $env:RWE_DESYNC_AT = "$desyncAt" }

    # Each peer says one line, at a tick of its own so the order is known:
    # nobody is at these keyboards, and a line that arrives at every other
    # peer is the whole of what chat has to do.
    if ($chat) { $env:RWE_CHAT_TEST = "$(300 + (60 * $me)):hello from $($names[$me])" }

    # One peer lagged, the rest honest: RWE_SIM_LAG makes that peer sleep after
    # every tick, which is a machine that cannot keep up reproduced on one that
    # can. The effect on the others is the thing being measured.
    if ($lag) {
        $lagParts = $lag.Split(":")
        if ($lagParts.Count -eq 2 -and [int]$lagParts[0] -eq $me) { $env:RWE_SIM_LAG = $lagParts[1] }
    }

    $env:RWE_HASH_LOG = $hashes[$me]
    if ($bridge -and $me -eq 0) {
        # Redirected stdio, which Start-Process cannot give us: this is the
        # channel a launcher holds, and the whole point of -bridge is to drive
        # the rejoin through it rather than through the test environment
        # variable. Only peer 0, that being the peer entitled to speak for a
        # dropped player.
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = $exe
        $psi.Arguments = "$a --bridge"
        $psi.UseShellExecute = $false
        $psi.RedirectStandardInput = $true
        $psi.RedirectStandardOutput = $true
        $psi.WorkingDirectory = Split-Path $exe
        $script:bridgeProc = [System.Diagnostics.Process]::Start($psi)
        $procs += $script:bridgeProc
    } else {
        $procs += Start-Process -FilePath $exe -ArgumentList $a -PassThru
    }
    Remove-Item Env:\RWE_DESYNC_AT -ErrorAction SilentlyContinue
    Remove-Item Env:\RWE_CHAT_TEST -ErrorAction SilentlyContinue
    Remove-Item Env:\RWE_SIM_LAG -ErrorAction SilentlyContinue
    Remove-Item Env:\RWE_REJOIN_TEST -ErrorAction SilentlyContinue
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
    # With -rejoin what matters is what happens after the kill, so the wait
    # here is only as long as the drop takes to be declared.
    if (-not $rejoin) { Start-Sleep -Seconds ($seconds - $killAfter) }
} else {
    Start-Sleep -Seconds $seconds
}

if ($rejoin) {
    # Peer 0 announces the bundle when it reaches the tick the returning peer
    # has to arrive at: by then its recording holds exactly the ticks that peer
    # is missing, and every peer is stalled waiting for it.
    Write-Output "waiting for the rejoin to be agreed and the bundle named"
    $atTick = 0
    $bundleSrc = $null

    if ($bridge) {
        # The launcher's half, in three messages: the game says who it lost,
        # the launcher asks for them back, and the game says where the
        # recording they need is. Nothing here reads the log.
        $dropped = Wait-BridgeEvent "player-dropped" 60
        if (-not $dropped) {
            Write-Output "RESULT: the game never reported a drop over the bridge"
        } else {
            Start-Sleep -Seconds $rejoinAfter
            Write-Output "asking over the bridge for player $($dropped.player) back"
            $script:bridgeProc.StandardInput.WriteLine("{""command"":""rejoin"",""player"":$($dropped.player)}")
            $script:bridgeProc.StandardInput.Flush()
            $bundleEvent = Wait-BridgeEvent "rejoin-bundle" 120
            if ($bundleEvent) {
                $atTick = [int]$bundleEvent.tick
                $bundleSrc = $bundleEvent.file
            }
        }
    } else {
        $bundleLine = $null
        for ($i = 0; $i -lt 120; $i++) {
            Start-Sleep -Seconds 1
            $bundleLine = Select-String -Path $logs[0] -Pattern "REJOIN-BUNDLE" -ErrorAction SilentlyContinue |
                Select-Object -Last 1
            if ($bundleLine) { break }
        }
        if ($bundleLine) {
            $m = [regex]::Match($bundleLine.Line, "tick=(?<tick>\d+) file=(?<file>.+)$")
            if (-not $m.Success) { throw "Could not read the bundle line: $($bundleLine.Line)" }
            $atTick = [int]$m.Groups["tick"].Value
            $bundleSrc = $m.Groups["file"].Value.Trim()
        }
    }

    if (-not $bundleSrc) {
        Write-Output "RESULT: no bundle was named; the rejoin never got that far"
    } else {

        # Copied rather than read in place, because the host is still writing
        # to it. This is the step a lobby would do over its own connection.
        $bundle = "$outDir\rejoin-bundle.rwereplay"
        Copy-Item -LiteralPath $bundleSrc -Destination $bundle -Force
        Write-Output "bundle at tick $atTick copied from $bundleSrc"

        $a = "--map `"$map`" --width 320 --height 240 --seed 7 --port $($basePort + $kill)"
        for ($p = 0; $p -lt $peers; $p++) {
            $side = $sides[$p % 2]
            if ($p -eq $kill) { $a += " --player `"$($names[$p]);Human;$side;$p`"" }
            else              { $a += " --player `"$($names[$p]);Network,[::1]:$($basePort + $p);$side;$p`"" }
        }
        if ($ai) { $a += " --player `"Computer;Computer;$($sides[$peers % 2]);$peers`"" }
        $a += " --log `"$($logs[$kill])`" --rejoin `"$bundle`" --rejoin-tick $atTick"

        $env:RWE_HASH_LOG = $hashes[$kill]
        if ($lag) {
            $lagParts = $lag.Split(":")
            if ($lagParts.Count -eq 2 -and [int]$lagParts[0] -eq $kill) { $env:RWE_SIM_LAG = $lagParts[1] }
        }
        $procs[$kill] = Start-Process -FilePath $exe -ArgumentList $a -PassThru
        Remove-Item Env:\RWE_HASH_LOG -ErrorAction SilentlyContinue
        Remove-Item Env:\RWE_SIM_LAG -ErrorAction SilentlyContinue
        Write-Output "player $kill restarted to rejoin at tick $atTick, pid $($procs[$kill].Id)"

        for ($i = 0; $i -lt 200; $i++) {
            Start-Sleep -Milliseconds 250
            if (Move-Offscreen $procs[$kill].Id) { break }
        }

        Start-Sleep -Seconds $seconds
        $rejoinSucceeded = $true
    }
}

# A rejoin run compares the peer that left as well: that it agrees with the
# peers that stayed over the whole game, the ticks it was not there for
# included, is the only thing that says the rejoin worked.
$survivors = if ($rejoinSucceeded) { 0..($peers - 1) } else { 0..($peers - 1) | Where-Object { $_ -ne $kill } }
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

# Each peer's own account of the run, beside the hash comparison above. The
# numbers say whether a lagged peer slowed the game smoothly or stalled it stop
# start, which is the question -lag is asked to answer.
foreach ($s in 0..($peers - 1)) {
    $summaries = Get-Content $logs[$s] -ErrorAction SilentlyContinue |
        Select-String -Pattern "Lockstep summary:"
    if ($summaries) {
        Write-Output "=================== player $s lockstep ==================="
        $summaries
    }
}

foreach ($s in 0..($peers - 1)) {
    $interesting = Get-Content $logs[$s] -ErrorAction SilentlyContinue |
        Select-String -Pattern "waiting for|quiet for|dropped|left the game|Desync|diverged at tick|critical|Chat from player"
    if ($interesting) {
        Write-Output "=================== player $s ==================="
        $interesting | Select-Object -First 10
    }
}
