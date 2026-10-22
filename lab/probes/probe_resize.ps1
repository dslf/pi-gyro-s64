# Probe: does the viewer repaint while the OS changes the window size, without the app polling?
# Programmatic SetWindowPos delivers WM_SIZE/WM_PAINT through GLFW's wndproc, so the refresh
# callback draws with the new framebuffer size even though glfwPollEvents is never reached.
# Checks: exit 0, stderr empty, fps reported, repaint counter >= number of resize bursts, the
# settled frame sizes follow the window (grow then shrink), no GL errors.
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class W {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr arg);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder sb, int max);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
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
}
'@
# Repo root is two levels above this script; run it from anywhere: powershell -File lab/probes/probe_resize.ps1
$dir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$out = Join-Path $PSScriptRoot 'probe_resize.log'
$errf = Join-Path $PSScriptRoot 'probe_resize.err'
if (Test-Path $out) { Remove-Item $out }
if (Test-Path $errf) { Remove-Item $errf }
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = Join-Path $dir 'viewer.exe'
$psi.Arguments = 'assets/cube.obj --idle-report 4'
$psi.WorkingDirectory = $dir
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$p = [System.Diagnostics.Process]::Start($psi)
Start-Sleep -Milliseconds 1500
$h = [W]::Find('pi-gyro')
if ($h -eq [IntPtr]::Zero) { Write-Output 'FAIL window not found by title prefix'; exit 1 }
$SWP = 0x0002 -bor 0x0010   # NOZORDER | NOMOVE
[W]::SetWindowPos($h, [IntPtr]::Zero, 0, 0, 1280, 900, $SWP) | Out-Null
Write-Output 'resized to 1280x900 (client grows by ~480 px of new area)'
Start-Sleep -Milliseconds 900
[W]::SetWindowPos($h, [IntPtr]::Zero, 0, 0, 700, 480, $SWP) | Out-Null
Write-Output 'resized to 700x480 (client shrinks)'
$p.WaitForExit()
$text = $p.StandardOutput.ReadToEnd()
$err = $p.StandardError.ReadToEnd()
if (Test-Path $out) { Remove-Item $out }
if (Test-Path $errf) { Remove-Item $errf }
$text | Set-Content $out   # kept for eyeballing
$sizes = @()
foreach ($m in [regex]::Matches($text, 'resize: (\d+)x(\d+) \(repaints during the drag: (\d+)\)')) {
  $sizes += [pscustomobject]@{ w = [int]$m.Groups[1].Value; h = [int]$m.Groups[2].Value; r = [int]$m.Groups[3].Value }
}
$idle = [regex]::Match($text, 'idle: (\d+) frames in ([\d.]+) s, fps ([\d.]+), resize repaints (\d+)')
$fails = 0
Write-Output ('exit: ' + $p.ExitCode)
if ($p.ExitCode -ne 0) { $fails++ ; Write-Output 'FAIL exit code' }
if ($err) { $fails++; Write-Output ('FAIL stderr: ' + $err) } else { Write-Output 'ok   stderr empty (no GL errors during the resize bursts)' }
if (-not $idle.Success) { $fails++; Write-Output 'FAIL idle report line missing' } else {
  Write-Output ('ok   idle: ' + $idle.Groups[1].Value + ' frames, fps ' + $idle.Groups[3].Value + ', repaints ' + $idle.Groups[4].Value)
  if ([double]$idle.Groups[3].Value -lt 30) { $fails++; Write-Output 'FAIL fps below 30 during idle run' }
  if ([int]$idle.Groups[4].Value -lt 2) { $fails++; Write-Output ('FAIL repaints ' + $idle.Groups[4].Value + ' < 2 resize bursts (WM_PAINT path not wired)') }
  else { Write-Output 'ok   repaints come from WM_PAINT, not from the poll loop (>= 2 bursts)' }
}
if ($sizes.Count -lt 2) { $fails++; Write-Output ('FAIL only ' + $sizes.Count + ' settled resize lines') } else {
  $first = $sizes[0]; $second = $sizes[1]
  Write-Output ('sizes after each burst: ' + ($sizes | ForEach-Object { "$($_.w)x$($_.h)" }) -join ' -> ')
  if ($first.w -gt $second.w -and $first.h -gt $second.h) { Write-Output 'ok   frame size follows the window: grown client larger than shrunk one' }
  else { $fails++; Write-Output ('FAIL size did not follow the window: ' + "$($first.w)x$($first.h)" + ' then ' + "$($second.w)x$($second.h)") }
  if ($first.r -ge 1 -and $second.r -gt $first.r) { Write-Output 'ok   repaint counter advanced between bursts' }
  else { $fails++; Write-Output ('FAIL repaint counter did not advance: ' + $first.r + ' -> ' + $second.r) }
}
if ($text -match 'resize: live repaint') { Write-Output 'ok   resize repaint path announced at startup' } else { $fails++; Write-Output 'FAIL startup banner missing' }
Write-Output ($(if ($fails -eq 0) { 'resize probe: PASS' } else { 'resize probe: FAIL (' + $fails + ')' }))
exit $(if ($fails -eq 0) { 0 } else { 1 })

