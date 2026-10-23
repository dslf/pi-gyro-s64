# probe_shot.ps1 -- real-window frame for the README screenshot, through the app's own `S` hotkey.
# The viewer writes screenshot.bmp from the window's default framebuffer, so the HUD is inside the
# frame and no desktop background or DPI rescaling sneaks in (a screen grab would add both).
# Checks: settings.ini moved aside so the frame shows built-in defaults (and restored afterwards),
# stdout says the teapot was loaded and the shot was saved at the framebuffer size, the BMP has
# model silhouette coverage and ink in all three HUD cards, PNG conversion ran, exit 0, stderr empty.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class W {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr arg);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder sb, int max);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern uint MapVirtualKeyW(uint vk, uint mapType);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr a);
  public static IntPtr Find(string pfx) {
    IntPtr hit = IntPtr.Zero;
    EnumWindows(new EnumWindowsProc((h, a) => {
      if (!IsWindowVisible(h)) return true;
      var sb = new StringBuilder(200);
      GetWindowTextW(h, sb, 200);
      if (sb.ToString().StartsWith(pfx)) { hit = h; return false; }
      return true;
    }), IntPtr.Zero);
    return hit;
  }
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  public static void Key(IntPtr h, byte vk) {
    // GLFW translates WM_KEYDOWN through the hardware scan code in lParam bits 16..23: lParam=0
    // arrives as GLFW_KEY_UNKNOWN and on_key never sees the key. MapVirtualKeyW gives the scan
    // code of this keyboard layout (MAPVK_VK_TO_VSC, then the extended variant if that is 0).
    uint scan = MapVirtualKeyW(vk, 0);
    if (scan == 0) scan = MapVirtualKeyW(vk, 4);
    IntPtr down = (IntPtr)(1L | ((ulong)scan << 16));
    IntPtr up = (IntPtr)(1L | ((ulong)scan << 16) | (1L << 32));
    PostMessage(h, 0x0100, (IntPtr)vk, down);   // WM_KEYDOWN
    PostMessage(h, 0x0101, (IntPtr)vk, up);     // WM_KEYUP
  }
  public static void Tap(byte vk) {                 // injected hardware key, fallback path
    keybd_event(vk, 0, 0, IntPtr.Zero);
    keybd_event(vk, 0, 2, IntPtr.Zero);             // KEYEVENTF_KEYUP
  }
}
'@
$dir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$shot = Join-Path $dir 'screenshot.bmp'          # the app writes it into the cwd
$art = Join-Path $PSScriptRoot 'probe_shot.bmp'  # artifact kept in the probe directory
$ini = Join-Path $dir 'settings.ini'
$inibak = Join-Path $PSScriptRoot 'probe_shot_settings.ini.bak'
$fails = 0
function Ok([string]$m) { Write-Output "ok   $m" }
function Fail([string]$m) { $script:fails++; Write-Output "FAIL $m" }

if (Test-Path $shot) { Remove-Item $shot }
# Defaults for the published frame: the live settings.ini next to viewer.exe moves aside.
$hadIni = Test-Path $ini
if ($hadIni) { Copy-Item $ini $inibak -Force; Remove-Item $ini }
Write-Output ("settings.ini aside for defaults: " + $hadIni)

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = Join-Path $dir 'viewer.exe'
$psi.Arguments = 'assets/utah_teapot.obj --size 1280 800'
$psi.WorkingDirectory = $dir
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$p = [System.Diagnostics.Process]::Start($psi)
Start-Sleep -Milliseconds 1600
$h = [W]::Find('pi-gyro')
if ($h -eq [IntPtr]::Zero) { Fail 'window not found by title prefix'; $p.Kill(); exit 1 }
if (-not [W]::SetForegroundWindow($h)) { Write-Output 'note: SetForegroundWindow declined (focus lock), key is posted anyway' }
Start-Sleep -Milliseconds 250
[W]::Key($h, 0x53)                               # VK 'S' -> snapshot() of the default framebuffer
$got = $false
for ($i = 0; $i -lt 12; $i++) {
  Start-Sleep -Milliseconds 150
  if (Test-Path $shot) { $got = $true; break }
}
if (-not $got) {
  Write-Output 'note: posted WM_KEYDOWN produced no file, retrying with an injected hardware key'
  [W]::SetForegroundWindow($h) | Out-Null
  [W]::Tap(0x53)
  for ($i = 0; $i -lt 12; $i++) {
    Start-Sleep -Milliseconds 150
    if (Test-Path $shot) { $got = $true; break }
  }
}
if (-not $got) { Fail 'no screenshot.bmp after VK S'; $p.Kill(); exit 1 }
Ok ("screenshot.bmp written by the app's own S hotkey ($((Get-Item $shot).Length) bytes)")
[W]::Key($h, 0x51)                               # VK 'Q' -> the app quits itself (Esc/Q quit)
if (-not $p.WaitForExit(6000)) { Fail 'app did not exit after Q'; $p.Kill() }
$out = $p.StandardOutput.ReadToEnd(); $err = $p.StandardError.ReadToEnd()
Write-Output ("exit: " + $p.ExitCode)
if ($p.ExitCode -ne 0) { Fail ("exit code " + $p.ExitCode) }
if ($err) { Fail ("stderr: $err") } else { Ok 'stderr empty (no GL errors during the shot)' }

# The app logs what it loaded and what it saved; the frame must be the teapot at the framebuffer size.
$m = [regex]::Match($out, 'saved (\S+) \((\d+)x(\d+)\)')
if (-not $m.Success) { Fail "no 'saved screenshot.bmp (WxH)' line in stdout"; $p | Out-Null }
else {
  Ok ("app reported $($m.Groups[1].Value) at $($m.Groups[2].Value)x$($m.Groups[3].Value)")
  if ($out -notmatch 'utah_teapot') { Fail 'stdout does not mention utah_teapot.obj' } else { Ok 'stdout names assets/utah_teapot.obj' }
}
if ($out -match 'idle|FAIL|GL error') { Fail "unexpected stdout marker: idle/FAIL/GL error" }

# Read the 24-bit BMP and measure it. Rows are bottom-up in the file; the copy flips them once.
$b = [System.IO.File]::ReadAllBytes($shot)
$off = [BitConverter]::ToInt32($b, 10); $w = [BitConverter]::ToInt32($b, 18); $hh = [BitConverter]::ToInt32($b, 22)
$bpp = [BitConverter]::ToInt16($b, 28)
if ($bpp -ne 24) { Fail ("BMP is $bpp bpp, expected 24") }
if ([BitConverter]::ToInt32($b, 30) -ne 0) { Fail "BMP compression is $([BitConverter]::ToInt32($b, 30)), expected BI_RGB (0)" }
if ($m.Success -and ($w -ne [int]$m.Groups[2].Value -or $hh -ne [int]$m.Groups[3].Value)) {
  Fail ("BMP $($w)x$($hh) != reported $([int]$m.Groups[2].Value)x$([int]$m.Groups[3].Value)")
}
Ok ("BMP header: $w x $hh x $bpp bpp, pixel offset $off")
$stride = [int]((($w * 3) + 3) -band -4)
$px = New-Object byte[] ($w * $hh * 3)
for ($y = 0; $y -lt $hh; $y++) {
  [Array]::Copy($b, $off + ($hh - 1 - $y) * $stride, $px, $y * $w * 3, $w * 3)
}
function Lum([int]$x, [int]$y) {
  $i = ($y * $w + $x) * 3
  return [double]$px[$i] * 0.114 + [double]$px[$i + 1] * 0.587 + [double]$px[$i + 2] * 0.299
}
# Background reference from the four corners: bg_rgb is black by default, so silhouette coverage
# is measured against what the frame actually has behind the model, not against an assumption.
$cs = 4; $sum = 0.0
$xLast = $w - 1; $yLast = $hh - 1
foreach ($cy in @(0, $yLast)) {
  foreach ($cx in @(0, $xLast)) {
    for ($dy = 0; $dy -lt $cs; $dy++) {
      for ($dx = 0; $dx -lt $cs; $dx++) {
        $sum += Lum ([Math]::Min($xLast, $cx - $cs + 1 + $dx)) ([Math]::Min($yLast, $cy - $cs + 1 + $dy))
      }
    }
  }
}
$bg = $sum / (4 * $cs * $cs)
$dev = 12.0; $cover = 0; $bright = 0; $dark = 0; $bgDev = [int]$bg + $dev
for ($y = 0; $y -lt $hh; $y++) {
  $row = $y * $w * 3
  for ($x = 0; $x -lt $w; $x++) {
    $i = $row + $x * 3
    $l = [double]$px[$i] * 0.114 + [double]$px[$i + 1] * 0.587 + [double]$px[$i + 2] * 0.299
    $mx = [Math]::Max([Math]::Max([int]$px[$i], [int]$px[$i + 1]), [int]$px[$i + 2])
    if ([Math]::Abs($l - $bg) -gt $dev -or $mx -gt $bgDev) { $cover++ }
    if ($l -gt 150) { $bright++ }
    if ($l -le ($bg + 2)) { $dark++ }
  }
}
$tot = $w * $hh
$covPct = 100.0 * $cover / $tot
Ok ("background corner luminance $([Math]::Round($bg, 1)), silhouette coverage $([Math]::Round($covPct, 2))% ($cover px), bright ink $bright px, untouched background $dark px")
if ($covPct -lt 2.0) { Fail ("silhouette coverage $([Math]::Round($covPct, 2))% < 2% - the model is not in the frame") }
if ($dark -ge $tot * 0.995) { Fail 'the frame is almost entirely background' }

# Three HUD cards: text is bright ink over a near-black 0.94-alpha card, so the card itself shows up
# as mid-dark pixels and the widgets/glyphs as bright ones. Regions are relative to the frame, which
# keeps the check valid if the display scale changes the framebuffer size.
function Card([string]$name, [int]$x0, [int]$x1, [int]$y0, [int]$y1, [int]$minInk, [int]$minCard) {
  $ink = 0; $cardpx = 0
  for ($y = $y0; $y -lt $y1; $y++) {
    $row = $y * $w * 3
    for ($x = $x0; $x -lt $x1; $x++) {
      $i = $row + $x * 3
      $l = [double]$px[$i] * 0.114 + [double]$px[$i + 1] * 0.587 + [double]$px[$i + 2] * 0.299
      if ($l -gt 150) { $ink++ }
      if ($l -ge 20 -and $l -le 90) { $cardpx++ }
    }
  }
  $area = ($x1 - $x0) * ($y1 - $y0)
  if ($ink -ge $minInk -and $cardpx -ge $minCard) {
    Ok ("$name card: ink $ink px, panel background $cardpx px of $area px")
  } else { Fail ("$name card: ink $ink px (need $minInk), panel background $cardpx px (need $minCard)") }
}
Card 'stats (top-left)' 8 ([int]($w * 0.30)) 8 ([int]($hh * 0.20)) 400 2000
Card 'settings (left column)' 8 ([int]($w * 0.45)) ([int]($hh * 0.20)) ([int]($hh * 0.78)) 400 8000
Card 'hints (bottom-centre)' ([int]($w * 0.20)) ([int]($w * 0.80)) ([int]($hh * 0.82)) $yLast 400 4000

# Publish the PNG (the only place README frames live) and keep the BMP as the probe artifact.
Copy-Item $shot $art -Force
Remove-Item $shot
Add-Type -AssemblyName System.Drawing
$png = Join-Path $dir 'docs/viewer-window.png'
$bmp = [System.Drawing.Bitmap]::FromFile($art)
$bmp.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Ok ("wrote $png ($((Get-Item $png).Length) bytes) from the $((Get-Item $art).Length) byte BMP; root screenshot.bmp removed")
$chk = [System.Drawing.Image]::FromFile($png)
if ($chk.Width -ne $w -or $chk.Height -ne $hh) { Fail ("PNG $($chk.Width)x$($chk.Height) != frame $w x $hh") } else { Ok ("PNG dimensions match the frame: $($chk.Width)x$($chk.Height)") }
$chk.Dispose()

# The live settings.ini goes back exactly as it was.
if ($hadIni) {
  Copy-Item $inibak $ini -Force; Remove-Item $inibak
  Ok "settings.ini restored from the backup ($((Get-Item $ini).Length) bytes)"
} else {
  if (Test-Path $ini) { Remove-Item $ini; Ok 'the app wrote a settings.ini during the run - removed, defaults stay uncreated' }
  else { Ok 'no settings.ini before or after the run (built-in defaults)' }
}
Write-Output ("probe_shot: " + $(if ($fails -eq 0) { 'PASS' } else { "FAIL ($fails)" }))
if ($fails -ne 0) { exit 1 }

