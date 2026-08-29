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

$p = Start-Process -FilePath "D:\RWE\build-release\rwe.exe" -WorkingDirectory "D:\RWE\build-release" -ArgumentList '--log','D:\RWE\rwe-vt.log','--map','"Coast To Coast"','--player','"Baile;Human;ARM;0"','--player','"Bot;Computer;CORE;1"' -PassThru
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

Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
"done"
