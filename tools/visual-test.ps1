# -extraArgs appends launch options, space separated, e.g.
# "--shading-strength-units 25 --shading-strength-buildings 40".
# -minimised keeps the game off the screen. The window is moved off the side
# of the desktop as soon as it appears, every click is posted to it rather than
# driven through the real mouse pointer, and every shot is taken by the game
# itself: Ctrl+F9 is posted, and the PCX the engine writes is decoded here.
# Nothing steals focus and nothing covers what you are doing. Use it whenever a
# run is not about how the window itself behaves.
#
# Two things had to be measured rather than assumed. A genuinely minimised
# window has a client rect of nothing, so its drawable is nothing, and the
# frame reads back black -- which is why this moves the window instead of
# minimising it; it is still in the task bar and still off the screen.
# And PrintWindow does not work on this window at all: the client area is an
# OpenGL surface, WM_PRINT does not reach it, and every frame it hands back is
# blank. glReadPixels through the engine's own screenshot key is what does
# work, off-screen and unfocused alike.
param([string]$phase = "build", [string]$outDir = "D:\RWE", [string]$tag = "", [string]$extraArgs = "", [switch]$minimised)
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
# System.Drawing is referenced explicitly: the PCX decoder below returns a
# Bitmap, and Add-Type does not bring that assembly in by itself.
Add-Type -ReferencedAssemblies System.Drawing @"
using System;
using System.Runtime.InteropServices;
public struct RECT { public int Left, Top, Right, Bottom; }
public struct POINT { public int X, Y; }
public static class W {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, UIntPtr e);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern IntPtr PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);

  // The engine writes 24-bit run-length PCX (Screenshot.cpp). Decoded here
  // rather than in PowerShell because a script loop over three planes of a
  // 1080p frame takes tens of seconds and this takes none.
  public static System.Drawing.Bitmap LoadPcx24(string path) {
    byte[] d = System.IO.File.ReadAllBytes(path);
    int w = BitConverter.ToUInt16(d, 8) - BitConverter.ToUInt16(d, 4) + 1;
    int h = BitConverter.ToUInt16(d, 10) - BitConverter.ToUInt16(d, 6) + 1;
    int planes = d[65];
    int bpl = BitConverter.ToUInt16(d, 66);
    var bmp = new System.Drawing.Bitmap(w, h, System.Drawing.Imaging.PixelFormat.Format24bppRgb);
    var data = bmp.LockBits(new System.Drawing.Rectangle(0, 0, w, h),
      System.Drawing.Imaging.ImageLockMode.WriteOnly, System.Drawing.Imaging.PixelFormat.Format24bppRgb);
    byte[] row = new byte[planes * bpl];
    byte[] line = new byte[data.Stride];
    int i = 128;
    for (int y = 0; y < h; y++) {
      int o = 0;
      while (o < row.Length) {
        byte b = d[i++];
        if (b >= 0xC0) { int n = b & 0x3F; byte v = d[i++]; for (int k = 0; k < n && o < row.Length; k++) row[o++] = v; }
        else row[o++] = b;
      }
      // Planar, red first; the bitmap wants BGR triples.
      for (int x = 0; x < w; x++) {
        line[x * 3 + 2] = row[x];
        line[x * 3 + 1] = row[bpl + x];
        line[x * 3 + 0] = row[2 * bpl + x];
      }
      Marshal.Copy(line, 0, (IntPtr)(data.Scan0.ToInt64() + (long)y * data.Stride), data.Stride);
    }
    bmp.UnlockBits(data);
    return bmp;
  }
}
"@

# SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE, and where off-screen is.
$SWP_MOVEONLY = 0x1 -bor 0x4 -bor 0x10
$OFFSCREEN_X = -4000
$OFFSCREEN_Y = -4000
# Where the engine puts its screenshots (util.cpp: APPDATA/RWE).
$script:shotDir = Join-Path $env:APPDATA "RWE\screenshots"

function Shot() {
  if ($script:minimisedRun -and $script:windowHandle) {
    # Ctrl+F9 posted to the window: the game reads its own back buffer with
    # glReadPixels once the frame is drawn, which is the one capture that
    # works on a window nobody can see. VK_CONTROL 0x11, VK_F9 0x78.
    $h = $script:windowHandle
    [W]::PostMessage($h, 0x0100, [IntPtr]0x11, [IntPtr]0x001D0001) | Out-Null
    Start-Sleep -Milliseconds 120
    [W]::PostMessage($h, 0x0100, [IntPtr]0x78, [IntPtr]0x00430001) | Out-Null
    Start-Sleep -Milliseconds 120
    [W]::PostMessage($h, 0x0101, [IntPtr]0x78, [IntPtr]0xC0430001) | Out-Null
    [W]::PostMessage($h, 0x0101, [IntPtr]0x11, [IntPtr]0xC01D0001) | Out-Null
    # The file appears a frame or two later, and is written in one go, so
    # wait for its size to stop changing as well as for it to exist.
    $file = $null
    for ($t = 0; $t -lt 60 -and -not $file; $t++) {
      Start-Sleep -Milliseconds 100
      $file = Get-ChildItem (Join-Path $script:shotDir "SHOT*.pcx") -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -notin $script:seenShots } | Select-Object -First 1
    }
    if (-not $file) { throw "the game did not answer Ctrl+F9" }
    $size = -1
    while ($file.Length -ne $size) { $size = $file.Length; Start-Sleep -Milliseconds 120; $file.Refresh() }
    $script:seenShots += $file.Name
    return [W]::LoadPcx24($file.FullName)
  }
  $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
  return $bmp
}
function Crop($bmp, $dst, $x, $y, $w, $h, $scale) {
  $o = New-Object System.Drawing.Bitmap ($w*$scale), ($h*$scale)
  $g = [System.Drawing.Graphics]::FromImage($o)
  $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
  $g.PixelOffsetMode = 'Half'
  $g.DrawImage($bmp, (New-Object System.Drawing.Rectangle 0,0,($w*$scale),($h*$scale)), (New-Object System.Drawing.Rectangle $x,$y,$w,$h), [System.Drawing.GraphicsUnit]::Pixel)
  $o.Save($dst); $o.Dispose()
}
function Hover($x, $y) {
  # Where the game thinks the pointer is. For an off-screen run that is a
  # posted WM_MOUSEMOVE in client coordinates -- moving the real pointer
  # would do nothing to a window nobody can see, and would take the user's
  # own pointer with it.
  if ($script:minimisedRun -and $script:windowHandle) {
    $o = Origin
    $l = [IntPtr]((([int]($y - $o.Y)) -shl 16) -bor (([int]($x - $o.X)) -band 0xFFFF))
    [W]::PostMessage($script:windowHandle, 0x0200, [IntPtr]::Zero, $l) | Out-Null
    Start-Sleep -Milliseconds 60
    return
  }
  [W]::SetCursorPos($x, $y) | Out-Null
}

function Key($spec) {
  # SendKeys goes to whatever has focus, which for an off-screen run is the
  # user's own work. Posted to the window instead, a key at a time: the
  # braced names SendKeys uses, "^a" for a control chord, anything else typed
  # as characters.
  if (-not ($script:minimisedRun -and $script:windowHandle)) {
    [System.Windows.Forms.SendKeys]::SendWait($spec)
    return
  }
  $h = $script:windowHandle
  $vk = @{ "{F2}" = 0x71; "{F9}" = 0x78; "{F10}" = 0x79; "{F11}" = 0x7A; "{ENTER}" = 0x0D; "{ESC}" = 0x1B; "{BREAK}" = 0x13; "{TAB}" = 0x09 }
  if ($vk.ContainsKey($spec)) {
    [W]::PostMessage($h, 0x0100, [IntPtr]$vk[$spec], [IntPtr]1) | Out-Null
    Start-Sleep -Milliseconds 80
    [W]::PostMessage($h, 0x0101, [IntPtr]$vk[$spec], [IntPtr]1) | Out-Null
    Start-Sleep -Milliseconds 120
    return
  }
  if ($spec.StartsWith("^")) {
    $c = [byte][char]([string]$spec.Substring(1)).ToUpper()
    [W]::PostMessage($h, 0x0100, [IntPtr]0x11, [IntPtr]1) | Out-Null
    Start-Sleep -Milliseconds 60
    [W]::PostMessage($h, 0x0100, [IntPtr]$c, [IntPtr]1) | Out-Null
    Start-Sleep -Milliseconds 60
    [W]::PostMessage($h, 0x0101, [IntPtr]$c, [IntPtr]1) | Out-Null
    [W]::PostMessage($h, 0x0101, [IntPtr]0x11, [IntPtr]1) | Out-Null
    Start-Sleep -Milliseconds 120
    return
  }
  # Plain text: the key down and up say which key, WM_CHAR is what a text
  # field actually reads.
  foreach ($ch in $spec.ToCharArray()) {
    $c = [byte][char]([string]$ch).ToUpper()
    [W]::PostMessage($h, 0x0100, [IntPtr]$c, [IntPtr]1) | Out-Null
    [W]::PostMessage($h, 0x0102, [IntPtr][int][char]$ch, [IntPtr]1) | Out-Null
    [W]::PostMessage($h, 0x0101, [IntPtr]$c, [IntPtr]1) | Out-Null
    Start-Sleep -Milliseconds 40
  }
  Start-Sleep -Milliseconds 120
}

function Click($x, $y) {
  if ($script:minimisedRun -and $script:windowHandle) {
    # Posted to the window in client coordinates: WM_MOUSEMOVE first, because
    # a click with no preceding move lands on whatever the game last thought
    # the pointer was over.
    $o = Origin
    $cx = [int]($x - $o.X)
    $cy = [int]($y - $o.Y)
    $l = [IntPtr](($cy -shl 16) -bor ($cx -band 0xFFFF))
    [W]::PostMessage($script:windowHandle, 0x0200, [IntPtr]::Zero, $l) | Out-Null
    Start-Sleep -Milliseconds 120
    [W]::PostMessage($script:windowHandle, 0x0201, [IntPtr]1, $l) | Out-Null
    Start-Sleep -Milliseconds 80
    [W]::PostMessage($script:windowHandle, 0x0202, [IntPtr]::Zero, $l) | Out-Null
    Start-Sleep -Milliseconds 400
    return
  }
  [W]::SetCursorPos($x, $y) | Out-Null
  Start-Sleep -Milliseconds 250
  [W]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 80; [W]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 400
}

$launchArgs = @('--log','D:\RWE\rwe-vt.log','--map','"Coast To Coast"','--player','"Baile;Human;ARM;0"','--player','"Bot;Computer;CORE;1"')
if ($extraArgs -ne "") { $launchArgs += ($extraArgs -split ' ') }
Remove-Item 'D:\RWE\rwe-vt.log' -ErrorAction SilentlyContinue
if ($phase -eq "info") {
  # 640x480 makes the window's client coordinates the same numbers as the
  # UI's own virtual coordinates, so a gadget's xpos/ypos out of the GUI file
  # is where you click. Everything in the footer is placed from SIDEDATA.TDF
  # in those coordinates, which is what this phase is looking at.
  $launchArgs += @('--width','640','--height','480')
  # Four Peewees dropped on the commander at six seconds: he kills them, and
  # the kills line under the damage bar is what wants looking at.
  $env:RWE_DEBUG_SPAWN = 'ARMPW*4@1:6:0'
}
if ($phase -eq "ring") {
  # An anti-nuke launcher next to the commander, so its coverage ring can be
  # looked at on the minimap.
  $launchArgs += @('--width','640','--height','480')
  $env:RWE_DEBUG_SPAWN = 'ARMAMD*1@0:8:0'
}
if ($phase -eq "shade") {
  # A solar collector dropped 96 units east of the commander at six seconds,
  # so both stand still for the camera without a click sequence that would
  # have to know where the commander is.
  $env:RWE_DEBUG_SPAWN = 'ARMSOLAR*1@0:6:0'
}
if ($phase -eq "solar") {
  # The same collector, but this phase turns it ON so the panels open. A
  # debug-spawned unit is not activated -- spawnCompletedUnit only calls
  # finishBuilding, which sets hit points and build time and nothing else --
  # so the panels stay shut until something issues the on/off order.
  $env:RWE_DEBUG_SPAWN = 'ARMSOLAR*1@0:6:0'
}
if ($phase -eq "mex") {
  # A metal extractor beside the commander, for the building halo.
  #
  # This phase exists to check one thing. TA's purple halo is an artefact of
  # the CACHED bitmap's anti-aliasing, and a piece the script marks DONT_CACHE
  # is not in that bitmap -- it is drawn straight to the screen each frame and
  # never goes through the table that produces the halo. The extractor's top
  # is such a piece. So the halo must appear along the base and stop at the
  # top, and a run where the whole extractor is fringed is the regression this
  # phase catches.
  $env:RWE_DEBUG_SPAWN = 'ARMMEX*1@0:6:0'
}
# The log is what says the scene is up, so last run's copy has to go first:
# left in place, every run decides it has arrived the moment it starts and
# photographs the loading screen instead of the game.
Remove-Item 'D:\RWE\rwe-vt.log' -ErrorAction SilentlyContinue
$p = Start-Process -FilePath "D:\RWE\build-release\rwe.exe" -WorkingDirectory "D:\RWE\build-release" -ArgumentList $launchArgs -PassThru
# The window can take a while to appear when a build is running alongside, so
# poll for it rather than trust a fixed wait, and then give the game time to
# get from the loading screen into the scene. A launch that never shows a
# window is killed rather than left running: a stray rwe.exe holds the DLLs
# the next build wants to copy.
$proc = $null
for ($w = 0; $w -lt 120 -and -not $proc; $w++) {
  Start-Sleep -Seconds 1
  $proc = Get-Process rwe -ErrorAction SilentlyContinue | Where-Object { $_.Id -eq $p.Id -and $_.MainWindowHandle -ne 0 } | Select-Object -First 1
}
if (-not $proc) { "no window"; Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; exit 1 }
# Loading can take a minute when the machine is busy, and a fixed wait has
# caught the loading screen more than once. The simulation only runs inside
# the game scene, so the first line it logs -- the AI's half-minute status,
# or a debug spawn -- says the scene is up.
$inScene = $false
for ($w = 0; $w -lt 240 -and -not $inScene; $w++) {
  Start-Sleep -Seconds 1
  $inScene = (Test-Path 'D:\RWE\rwe-vt.log') -and (Select-String -Path 'D:\RWE\rwe-vt.log' -Pattern 'AI player . status|Debug: spawning' -Quiet)
}
if (-not $inScene) { "never reached the scene"; Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; exit 1 }
Start-Sleep -Seconds 2
$h = $proc.MainWindowHandle
$script:windowHandle = $h
$script:minimisedRun = [bool]$minimised
$script:seenShots = @()
if ($minimised) {
  # Its own screenshots, so the ones already there are not mistaken for this
  # run's: the engine numbers from one past the highest it finds.
  if (Test-Path $script:shotDir) {
    $script:seenShots = @(Get-ChildItem (Join-Path $script:shotDir "SHOT*.pcx") | ForEach-Object { $_.Name })
  }
  [W]::SetWindowPos($h, [IntPtr]::Zero, $OFFSCREEN_X, $OFFSCREEN_Y, 0, 0, $SWP_MOVEONLY) | Out-Null
  "window moved off the screen; the game takes its own shots"
} else {
  [W]::SetForegroundWindow($h) | Out-Null
}
Start-Sleep -Milliseconds 500
function Origin() {
  # For a minimised run the shot IS the client area, so its origin is 0,0 and
  # every coordinate the phases use is already relative to it.
  if ($script:minimisedRun) { return (New-Object POINT) }
  # Asked for afresh before each click and shot: the window has been seen
  # to move after it first appears, and a stale origin puts every crop and
  # click somewhere else.
  $pt = New-Object POINT; [W]::ClientToScreen($h, [ref]$pt) | Out-Null
  return $pt
}
$o = Origin
"client origin $($o.X),$($o.Y)"

if ($phase -eq "build") {
  # Commander builds a solar collector in front of him: watch the three construction phases.
  Click ($o.X + 465) ($o.Y + 156)
  Click ($o.X + 465) ($o.Y + 156)
  Click ($o.X + 92) ($o.Y + 141)
  Click ($o.X + 32) ($o.Y + 189)
  Click ($o.X + 300) ($o.Y + 250)
  Hover ($o.X + 700) ($o.Y + 550)
  for ($i = 0; $i -lt 10; $i++) {
    Start-Sleep -Seconds 2
    $bmp = Shot
    if ($i -eq 0) { $bmp.Save("D:\RWE\vt-full0.png") }
    Crop $bmp ("D:\RWE\vt-build$i.png") ($o.X + 200) ($o.Y + 80) 300 240 3
    $bmp.Dispose()
  }
}

if ($phase -eq "air") {
  # Spawn an Atlas through the debug window, then order it across the map and watch it fly.
  Key "{F10}"
  Start-Sleep -Milliseconds 800
  Click ($o.X + 540) ($o.Y + 311)
  Key "ARMATLAS"
  Start-Sleep -Milliseconds 300
  Hover ($o.X + 560) ($o.Y + 330)
  Start-Sleep -Milliseconds 300
  Key "{ENTER}"
  Start-Sleep -Milliseconds 800
  # close the debug window with its X button (keys are swallowed while the text field has focus)
  Click ($o.X + 737) ($o.Y + 74)
  Start-Sleep -Milliseconds 600
  $bmp = Shot; Crop $bmp "D:\RWE\vt-air0.png" ($o.X + 130) ($o.Y + 40) 660 520 2; $bmp.Dispose()
  # select it (it spawned under the cursor) and send it to the top-left of the view
  Click ($o.X + 560) ($o.Y + 330)
  Start-Sleep -Milliseconds 300
  Click ($o.X + 32) ($o.Y + 389)
  Click ($o.X + 250) ($o.Y + 120)
  for ($i = 1; $i -le 12; $i++) {
    Start-Sleep -Milliseconds 700
    $bmp = Shot
    Crop $bmp ("D:\RWE\vt-air$i.png") ($o.X + 130) ($o.Y + 40) 660 520 2
    $bmp.Dispose()
  }
}


if ($phase -eq "ship") {
  # A Hulk in the water and a Peewee on the shore: LOAD should swing the crane out.
  Key "{F10}"
  Start-Sleep -Milliseconds 800
  Click ($o.X + 540) ($o.Y + 311)
  Key "^a"
  Key "ARMTSHIP"
  Start-Sleep -Milliseconds 300
  Hover ($o.X + 150) ($o.Y + 300)
  Start-Sleep -Milliseconds 300
  Key "{ENTER}"
  Start-Sleep -Milliseconds 600
  Click ($o.X + 540) ($o.Y + 311)
  Key "^a"
  Key "ARMPW"
  Start-Sleep -Milliseconds 300
  Hover ($o.X + 270) ($o.Y + 300)
  Start-Sleep -Milliseconds 300
  Key "{ENTER}"
  Start-Sleep -Milliseconds 600
  Click ($o.X + 737) ($o.Y + 74)
  Start-Sleep -Milliseconds 600
  $bmp = Shot; $bmp.Save("D:\RWE\vt-shipfull.png"); Crop $bmp "D:\RWE\vt-ship0.png" ($o.X + 40) ($o.Y + 180) 400 260 2; $bmp.Dispose()
  Click ($o.X + 150) ($o.Y + 300)
  Start-Sleep -Milliseconds 300
  $bmp = Shot; $bmp.Save("D:\RWE\vt-shipsel.png"); $bmp.Dispose()
  Click ($o.X + 90) ($o.Y + 459)
  Click ($o.X + 270) ($o.Y + 300)
  Hover ($o.X + 700) ($o.Y + 550)
  for ($i = 1; $i -le 8; $i++) {
    Start-Sleep -Seconds 1
    $bmp = Shot
    Crop $bmp ("D:\RWE\vt-ship$i.png") ($o.X + 40) ($o.Y + 180) 400 260 2
    $bmp.Dispose()
  }
  # Now unload it back onto the shore a little further along.
  Click ($o.X + 150) ($o.Y + 300)
  Click ($o.X + 90) ($o.Y + 314)
  $bmp = Shot; $bmp.Save("D:\RWE\vt-unloadfull.png"); $bmp.Dispose()
  Click ($o.X + 290) ($o.Y + 340)
  Hover ($o.X + 700) ($o.Y + 550)
  for ($i = 1; $i -le 10; $i++) {
    Start-Sleep -Seconds 1
    $bmp = Shot
    Crop $bmp ("D:\RWE\vt-unload$i.png") ($o.X + 40) ($o.Y + 180) 400 260 2
    $bmp.Dispose()
  }
}

if ($phase -eq "info") {
  # The footer is the bottom 40 rows of a 640x480 client area and the left
  # panel the first 128 columns; every crop below is one or the other.
  function Footer($name) {
    $bmp = Shot
    Crop $bmp ("D:\RWE\vt-info-$name.png") $o.X ($o.Y + 440) 640 40 3
    $bmp.Dispose()
  }
  function Panel($name) {
    $bmp = Shot
    Crop $bmp ("D:\RWE\vt-info-$name.png") $o.X $o.Y 140 480 2
    $bmp.Dispose()
  }
  function Full($name) {
    $bmp = Shot
    $bmp.Save("D:\RWE\vt-info-$name.png")
    $bmp.Dispose()
  }

  # 1. Hover the commander, who starts at the centre of the world viewport
  #    (x 128..640, y 32..448 on this window). Give the Peewees time to
  #    arrive and die first.
  Hover ($o.X + 390) ($o.Y + 158)
  Start-Sleep -Seconds 45
  Hover ($o.X + 391) ($o.Y + 159)
  Start-Sleep -Seconds 2
  Footer "hover"
  Full "hoverfull"

  # 2. Select him and start a solar collector: the mission line should read
  #    Nanolathing and the second name-and-bar slot should fill in.
  Click ($o.X + 390) ($o.Y + 158)
  Start-Sleep -Seconds 1
  Panel "buildpage"
  # ARMSOLAR is at (0,27) inside the build page and the page sits at (0,128).
  Click ($o.X + 32) ($o.Y + 187)
  Start-Sleep -Milliseconds 400
  Click ($o.X + 330) ($o.Y + 300)

  # 2b. The builder, caught while it is still working: the mission line
  #      should read Nanolathing and the second name-and-bar slot should name
  #      what it is building and show how far along it is. A solar collector
  #      goes up in a few seconds, so the game is paused first and the ground
  #      around the site swept at leisure -- the footer is drawn from live
  #      state and the cursor still moves while the simulation is stopped.
  Start-Sleep -Seconds 7
  Key "{BREAK}"
  Start-Sleep -Milliseconds 800
  $k = 0
  foreach ($dy in @(-73, -60, -85, -45, -30, 0)) {
    foreach ($dx in @(10, 0, 25, -15, 45)) {
      Hover ($o.X + 330 + $dx) ($o.Y + 300 + $dy)
      Start-Sleep -Milliseconds 250
      Footer ("builder$k")
      $k = $k + 1
    }
  }
  Key "{BREAK}"
  Start-Sleep -Milliseconds 500

  Panel "buildqueue"
  Full "buildingfull"

  # 3. Hover the finished collector.
  Hover ($o.X + 330) ($o.Y + 300)
  Start-Sleep -Seconds 2
  Footer "nanoframe"

  # 4. An anti-nuke launcher, placed through the debug window, draws its
  #    coverage ring on the minimap while it is selected.
  Key "{F10}"
  Start-Sleep -Milliseconds 900
  Full "debugwindow"

  # 5. The save dialog: the text caret belongs to the focused control, and
  #    the selected list row is brightened.
  Key "{F10}"
  Start-Sleep -Milliseconds 600
  Key "{F2}"
  Start-Sleep -Milliseconds 900
  Full "menu"
  # SAVEGAME sits at (13,64) inside the menu page, which is at (0,128).
  Click ($o.X + 61) ($o.Y + 207)
  Start-Sleep -Milliseconds 900
  Full "savedialog"
  # LOAD is the OK button of the shared dialog: (353,324) inside a panel at
  # (81,27).
  Click ($o.X + 482) ($o.Y + 361)
  Start-Sleep -Seconds 2
  Click ($o.X + 61) ($o.Y + 207)
  Start-Sleep -Milliseconds 900
  # GAMES is at (65,71) inside the same panel, so the first row starts at
  # (146,98).
  Click ($o.X + 200) ($o.Y + 104)
  Start-Sleep -Milliseconds 600
  Full "savelist"
}

if ($phase -eq "ring") {
  # Ctrl+A only takes units that can move, so the launcher has to be clicked
  # on. It is dropped 96 world units east of the commander, which at this
  # zoom on this map puts it here.
  Start-Sleep -Seconds 15
  Click ($o.X + 480) ($o.Y + 152)
  Start-Sleep -Seconds 2
  $bmp = Shot
  Crop $bmp "D:\RWE\vt-ring-minimap.png" $o.X ($o.Y + 28) 128 104 5
  $bmp.Save("D:\RWE\vt-ring-full.png")
  $bmp.Dispose()
}

if ($phase -eq "solar") {
  # Select the collector, then show what the left panel offers for it, so the
  # on/off gadget can be found rather than guessed at. The collector lands
  # about 95 pixels east of the commander at this window size.
  Hover ($o.X + 700) ($o.Y + 550)
  Start-Sleep -Seconds 8
  $o = Origin
  if (-not $script:minimisedRun) { [W]::SetForegroundWindow($h) | Out-Null }
  Start-Sleep -Milliseconds 300
  Click ($o.X + 560) ($o.Y + 155)
  # The on/off gadget, which reads OFF for a spawned collector. Its row sits
  # at client y 228: the panel's own tabs are at y 141, which the build phase
  # above already clicks, and the rows below them step at about 27 pixels.
  Click ($o.X + 62) ($o.Y + 228)
  Hover ($o.X + 700) ($o.Y + 550)
  # The panels are an animation, not a state change, so give the script time
  # to run them open before the shutter.
  Start-Sleep -Seconds 4
  $bmp = Shot
  Crop $bmp "$outDir\vt-solar$tag-selected.png" $o.X $o.Y 800 600 1
  Crop $bmp "$outDir\vt-solar$tag-panel.png" $o.X ($o.Y + 100) 130 400 3
  $bmp.Dispose()
  # Move the selection onto the commander rather than trying to clear it: a
  # click on bare ground leaves a building selected, so the box would still be
  # drawn round the collector in the close-up. Selecting something else puts
  # it somewhere harmless.
  Click ($o.X + 465) ($o.Y + 155)
  Hover ($o.X + 700) ($o.Y + 550)
  Start-Sleep -Seconds 2
  $bmp = Shot
  Crop $bmp "$outDir\vt-solar$tag-full.png" $o.X $o.Y 800 600 1
  Crop $bmp "$outDir\vt-solar$tag-open.png" ($o.X + 512) ($o.Y + 108) 105 100 6
  $bmp.Dispose()
}

if ($phase -eq "mex") {
  # Nothing to click: the extractor is spawned standing still, and the whole
  # question is what its edges look like. The mouse is parked out of the way
  # so no hover highlight lands on it.
  Hover ($o.X + 700) ($o.Y + 550)
  Start-Sleep -Seconds 8
  $o = Origin
  $bmp = Shot
  Crop $bmp "$outDir\vt-mex$tag-full.png" $o.X $o.Y 800 600 1
  $bmp.Dispose()
  # A second frame with the arm somewhere else, and this pair is the actual
  # test. The extractor does not move; only its arm turns. So a halo pixel
  # that is present in one frame and gone in the other, at a coordinate the
  # arm has swept across, is the fringe crawling about inside the model --
  # which is the bug this phase exists to catch, and which no single frame can
  # show. Compare the two with the checker in the halo section of S:101.
  Start-Sleep -Seconds 3
  $bmp = Shot
  Crop $bmp "$outDir\vt-mex$tag-full-b.png" $o.X $o.Y 800 600 1
  $bmp.Dispose()
}


if ($phase -eq "shade") {
  # The commander and the collector spawned beside him, both standing still
  # for the camera. The collector is the reference case for the model
  # shading (TOTALA-EXE-SHADING.md S:13): its left panel sits at rows 28-29
  # and its right panel at row 0, solid black, and the skirt between them is
  # where the Gouraud ramp through the middle of the table shows. -tag names
  # the build under test so a before and an after can sit side by side. On
  # Coast To Coast at this window size the commander starts near (505,165)
  # of the client area and the collector lands about 110 pixels east.
  Hover ($o.X + 700) ($o.Y + 550)
  Start-Sleep -Seconds 10
  $o = Origin
  if (-not $script:minimisedRun) { [W]::SetForegroundWindow($h) | Out-Null }
  Start-Sleep -Milliseconds 300
  $bmp = Shot
  # The client area alone, never the whole screen: the desktop around the
  # window is not under test.
  Crop $bmp "$outDir\vt-shade$tag-full.png" $o.X $o.Y 800 600 1
  Crop $bmp "$outDir\vt-shade$tag-scene.png" ($o.X + 400) ($o.Y + 90) 300 160 3
  Crop $bmp "$outDir\vt-shade$tag-solar.png" ($o.X + 570) ($o.Y + 120) 90 90 6
  Crop $bmp "$outDir\vt-shade$tag-commander.png" ($o.X + 470) ($o.Y + 120) 70 90 6
  $bmp.Dispose()
}

if ($phase -eq "stall") {
  # The commander builds a metal maker below himself: page one of his menu
  # (ARMCOM1.GUI) puts ARMMAKR at (64,155) in a page at (0,128), and a
  # finished maker switches itself on and draws 60 energy a second against
  # a fresh start's 25, so the stockpile is empty within a minute. Then four
  # shots of the top bar a quarter of a second apart: a bar that flashes
  # shows up as frames that disagree, a steady one as four alike.
  $o = Origin
  Click ($o.X + 505) ($o.Y + 165)
  Click ($o.X + 96) ($o.Y + 315)
  Click ($o.X + 505) ($o.Y + 330)
  Hover ($o.X + 700) ($o.Y + 550)
  Start-Sleep -Seconds 75
  $o = Origin
  if (-not $script:minimisedRun) { [W]::SetForegroundWindow($h) | Out-Null }
  Start-Sleep -Milliseconds 300
  for ($i = 0; $i -lt 4; $i++) {
    $bmp = Shot
    if ($i -eq 0) { Crop $bmp "$outDir\vt-stall$tag-full.png" $o.X $o.Y 800 600 1 }
    Crop $bmp "$outDir\vt-stall$tag-$i.png" $o.X $o.Y 800 40 2
    $bmp.Dispose()
    Start-Sleep -Milliseconds 260
  }
}

Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
"done"
