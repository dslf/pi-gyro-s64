# lab/probes/probe_font.ps1 - what does the settings key 'hud_font_px' mean on screen?
# The key is a pixel size (ADR PROJECT_MEMORY 4.26): src/hud.c bakes the default font at
# style.FontSizeBase = HUD_FONT_BASE = 17 px and style.FontScaleMain = hud_font_px / HUD_FONT_BASE
# (io.DisplayFramebufferScale = 1), so ImGui GetFontSize() = hud_font_px. The legacy env name
# HUD_FONT_SCALE is still honoured as a multiplier over 17 px, so an old tuning script works.
# Measured on the real window through the app's own `S` hotkey: the help panel ink is counted from
# the framebuffer PNG, so the number is what the panel actually draws, not what the code claims.
# Sizes are driven with the HUD_FONT_PX env override that hud_init reads - settings.ini is never touched.
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
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, int msg, IntPtr w, IntPtr l);
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
  public static void Key(IntPtr h, int vk) {
    PostMessageW(h, 0x0100, (IntPtr)vk, (IntPtr)0);   // WM_KEYDOWN
    PostMessageW(h, 0x0101, (IntPtr)vk, (IntPtr)0);   // WM_KEYUP
  }
}
'@
$dir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$BASE = 17.00                    # src/hud.c HUD_FONT_BASE
# Measured: 12.41 px (the smallest size the owner read) and 17.00 px (what the pre-E5.T8 build drew).
$fails = 0
function Fail($m) { $script:fails++; Write-Output "FAIL $m" }

if (Test-Path (Join-Path $dir 'screenshot.bmp')) { Remove-Item (Join-Path $dir 'screenshot.bmp') }
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = Join-Path $dir 'viewer.exe'
$psi.Arguments = 'assets/utah_teapot.obj --size 1280 900'
$psi.WorkingDirectory = $dir
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true

function Measure([double]$sz) {
  $env:HUD_FONT_PX = "$sz"
  $p = [System.Diagnostics.Process]::Start($psi)
  $out = $p.StandardOutput.ReadToEnd()      # consumed while the window is up: the pipe would otherwise fill
  $err = $p.StandardError.ReadToEnd()
  Start-Sleep -Milliseconds 1500
  $h = [W]::Find('pi-gyro')
  if ($h -eq [IntPtr]::Zero) { Fail "no pi-gyro window at $sz px" } else {
    [W]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 300
    [W]::Key($h, 0x53)                      # S -> snapshot() writes screenshot.bmp
    Start-Sleep -Milliseconds 1500
  }
  $p.Kill()
  Remove-Item Env:HUD_FONT_PX
  $bmp = Join-Path $dir 'screenshot.bmp'
  if (-not (Test-Path $bmp)) { Fail "no screenshot.bmp at $sz px"; return $null }
  $img = [System.Drawing.Bitmap]::FromFile($bmp)
  $rect = [System.Drawing.Rectangle]::new(600, 560, 620, 300)   # the hotkey hints strip, bottom-centre
  $crop = $img.Clone($rect, $img.PixelFormat)
  $img.Dispose(); $crop.Save((Join-Path $PSScriptRoot 'probe_font_crop.png'), [System.Drawing.Imaging.ImageFormat]::Png)
  $bpp = [System.Drawing.Bitmap]::new((Join-Path $PSScriptRoot 'probe_font_crop.png'))
  $ink = 0; $lum = 0.0
  for ($y = 0; $y -lt $bpp.Height; $y++) {
    for ($x = 0; $x -lt $bpp.Width; $x++) {
      $c = $bpp.GetPixel($x, $y)
      $l = (0.2126 * $c.R + 0.7152 * $c.G + 0.0722 * $c.B) / 255.0
      if ($l -gt 0.25) { $ink++; $lum += $l }
    }
  }
  $bpp.Dispose()
  Remove-Item $bmp
  $hud = ($out -split "`n" | Where-Object { $_ -match 'font [\d.]+ px glyph' } | Select-Object -First 1)
  $line = ('{0,-7} ink {1,5} px  meanLum {2:F3}' -f "$sz px", $ink, ($lum / [Math]::Max(1, $ink)))
  if ($hud -notmatch 'px glyph') { Fail "no 'font .. px glyph' line in hud log at $sz px" }
  else { Write-Output ("  hud log: $($hud.Trim())") }
  if ($err) { Fail "stderr at $sz px" }
  Write-Output $line
  return @{ sz = $sz; ink = $ink }
}

$small = Measure 12.41
$big = Measure 17.00
if ($small -and $big) {
  $ratio = $small.ink / $big.ink
  # glyph area scales with px^2: 12.41^2 / 17^2 = 0.533; the ink count has AA bleed, so +-15 %
  $want = [Math]::Pow(12.41 / 17.00, 2)
  $ok = [Math]::Abs($ratio - $want) / $want -lt 0.15
  Write-Output ("ink ratio 12.41 px / 17 px = {0:F3} (area expectation {1:F3})" -f $ratio, $want)
  if (-not $ok) { Fail "ink ratio does not follow the glyph area" } else { Write-Output "ok   ink follows glyph area as the px size changes" }
}
Write-Output ("glyph px = HUD_FONT_PX as-is (atlas base {0} px, FontScaleMain = px / base), src/hud.c + settings.hud_font_px" -f $BASE)
if ($fails) { Write-Output "probe_font.ps1: $fails FAIL"; exit 1 } else { Write-Output "probe_font.ps1: PASS"; exit 0 }
