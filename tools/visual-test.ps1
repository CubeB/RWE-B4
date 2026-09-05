param([string]$phase = "build")
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type @"
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
}
"@

function Shot() {
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
function Click($x, $y) {
  [W]::SetCursorPos($x, $y) | Out-Null
  Start-Sleep -Milliseconds 250
  [W]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 80; [W]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 400
}

$launchArgs = @('--log','D:\RWE\rwe-vt.log','--map','"Coast To Coast"','--player','"Baile;Human;ARM;0"','--player','"Bot;Computer;CORE;1"')
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
$p = Start-Process -FilePath "D:\RWE\build-release\rwe.exe" -WorkingDirectory "D:\RWE\build-release" -ArgumentList $launchArgs -PassThru
Start-Sleep -Seconds 20
$proc = Get-Process rwe -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $proc) { "no window"; exit 1 }
$h = $proc.MainWindowHandle
[W]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 500
$o = New-Object POINT; [W]::ClientToScreen($h, [ref]$o) | Out-Null
"client origin $($o.X),$($o.Y)"

if ($phase -eq "build") {
  # Commander builds a solar collector in front of him: watch the three construction phases.
  Click ($o.X + 465) ($o.Y + 156)
  Click ($o.X + 465) ($o.Y + 156)
  Click ($o.X + 92) ($o.Y + 141)
  Click ($o.X + 32) ($o.Y + 189)
  Click ($o.X + 300) ($o.Y + 250)
  [W]::SetCursorPos($o.X + 700, $o.Y + 550) | Out-Null
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
  [System.Windows.Forms.SendKeys]::SendWait("{F10}")
  Start-Sleep -Milliseconds 800
  Click ($o.X + 540) ($o.Y + 311)
  [System.Windows.Forms.SendKeys]::SendWait("ARMATLAS")
  Start-Sleep -Milliseconds 300
  [W]::SetCursorPos($o.X + 560, $o.Y + 330) | Out-Null
  Start-Sleep -Milliseconds 300
  [System.Windows.Forms.SendKeys]::SendWait("{ENTER}")
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
  [System.Windows.Forms.SendKeys]::SendWait("{F10}")
  Start-Sleep -Milliseconds 800
  Click ($o.X + 540) ($o.Y + 311)
  [System.Windows.Forms.SendKeys]::SendWait("^a")
  [System.Windows.Forms.SendKeys]::SendWait("ARMTSHIP")
  Start-Sleep -Milliseconds 300
  [W]::SetCursorPos($o.X + 150, $o.Y + 300) | Out-Null
  Start-Sleep -Milliseconds 300
  [System.Windows.Forms.SendKeys]::SendWait("{ENTER}")
  Start-Sleep -Milliseconds 600
  Click ($o.X + 540) ($o.Y + 311)
  [System.Windows.Forms.SendKeys]::SendWait("^a")
  [System.Windows.Forms.SendKeys]::SendWait("ARMPW")
  Start-Sleep -Milliseconds 300
  [W]::SetCursorPos($o.X + 270, $o.Y + 300) | Out-Null
  Start-Sleep -Milliseconds 300
  [System.Windows.Forms.SendKeys]::SendWait("{ENTER}")
  Start-Sleep -Milliseconds 600
  Click ($o.X + 737) ($o.Y + 74)
  Start-Sleep -Milliseconds 600
  $bmp = Shot; $bmp.Save("D:\RWE\vt-shipfull.png"); Crop $bmp "D:\RWE\vt-ship0.png" ($o.X + 40) ($o.Y + 180) 400 260 2; $bmp.Dispose()
  Click ($o.X + 150) ($o.Y + 300)
  Start-Sleep -Milliseconds 300
  $bmp = Shot; $bmp.Save("D:\RWE\vt-shipsel.png"); $bmp.Dispose()
  Click ($o.X + 90) ($o.Y + 459)
  Click ($o.X + 270) ($o.Y + 300)
  [W]::SetCursorPos($o.X + 700, $o.Y + 550) | Out-Null
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
  [W]::SetCursorPos($o.X + 700, $o.Y + 550) | Out-Null
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
  [W]::SetCursorPos(($o.X + 390), ($o.Y + 158)) | Out-Null
  Start-Sleep -Seconds 45
  [W]::SetCursorPos(($o.X + 391), ($o.Y + 159)) | Out-Null
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
  [System.Windows.Forms.SendKeys]::SendWait("{BREAK}")
  Start-Sleep -Milliseconds 800
  $k = 0
  foreach ($dy in @(-73, -60, -85, -45, -30, 0)) {
    foreach ($dx in @(10, 0, 25, -15, 45)) {
      [W]::SetCursorPos(($o.X + 330 + $dx), ($o.Y + 300 + $dy)) | Out-Null
      Start-Sleep -Milliseconds 250
      Footer ("builder$k")
      $k = $k + 1
    }
  }
  [System.Windows.Forms.SendKeys]::SendWait("{BREAK}")
  Start-Sleep -Milliseconds 500

  Panel "buildqueue"
  Full "buildingfull"

  # 3. Hover the finished collector.
  [W]::SetCursorPos(($o.X + 330), ($o.Y + 300)) | Out-Null
  Start-Sleep -Seconds 2
  Footer "nanoframe"

  # 4. An anti-nuke launcher, placed through the debug window, draws its
  #    coverage ring on the minimap while it is selected.
  [System.Windows.Forms.SendKeys]::SendWait("{F10}")
  Start-Sleep -Milliseconds 900
  Full "debugwindow"

  # 5. The save dialog: the text caret belongs to the focused control, and
  #    the selected list row is brightened.
  [System.Windows.Forms.SendKeys]::SendWait("{F10}")
  Start-Sleep -Milliseconds 600
  [System.Windows.Forms.SendKeys]::SendWait("{F2}")
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

Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
"done"
