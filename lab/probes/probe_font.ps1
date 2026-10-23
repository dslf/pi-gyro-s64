# lab/probes/probe_font.ps1 - what pixel size does settings 'hud_font_scale' mean?
#
# src/hud.c bakes the ImGui default font with style.FontSizeBase = HUD_FONT_BASE (17 px) and
# style.FontScaleMain = hud_font_scale (io.DisplayFramebufferScale = 1, style.FontScaleDpi = 1), so
# imgui's GetFontSize() == FontSizeBase * FontScaleMain -> px = 17 * scale. The HUD selftest lays the
# panels out from the baked glyph metrics, so ink pixel counts and panel heights must follow the
# scale. --selftest does not read settings.ini (settings_default is used there), so the scale is set
# with the HUD_FONT_SCALE env override that hud_init reads - settings.ini is never touched.
#
#   powershell -File lab/probes/probe_font.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
Set-Location $root
$exe = Join-Path $root 'viewer.exe'
if (-not (Test-Path $exe)) { throw "missing $exe - run make first" }

$BASE = 17.0   # src/hud.c HUD_FONT_BASE
$scales = @(0.73, 0.78, 0.80, 1.00)
$rows = @()
$fail = @()

function Grab([string[]]$lines, [string]$pat) {
  return ($lines | Where-Object { $_ -match $pat } | Select-Object -First 1)
}
function InkOf([string]$line) {
  $m = [regex]::Match($line, 'ink (\d+) px of (\d+) x (\d+)')
  if (-not $m.Success) { return $null }
  return [pscustomobject]@{ink = [int]$m.Groups[1].Value; w = [int]$m.Groups[2].Value; h = [int]$m.Groups[3].Value}
}

foreach ($sc in $scales) {
  $env:HUD_FONT_SCALE = "$sc"
  $ea = $ErrorActionPreference; $ErrorActionPreference = 'Continue'   # native stderr is a warning line, not a failure
  $out = (& $exe --selftest 2>$null | Out-String) -split "`n"
  $ErrorActionPreference = $ea
  $code = $LASTEXITCODE
  Remove-Item Env:HUD_FONT_SCALE

  $hud = Grab $out '^hud: imgui'
  $help = Grab $out 'help ink \d+ px'
  $hints = Grab $out '^  ok   hint lines'
  $rows += [pscustomobject]@{scale = $sc; px = [math]::Round($BASE * $sc, 2); hud = ($hud -split '\n')[0];
                              help = InkOf $help; helpLine = ($help -split '\n')[0]; hints = ($hints -split '\n')[0]; code = $code }
  if ($code -ne 0) { $fail += "scale $sc : selftest exit $code" }
  if ($hud -notmatch ('font scale {0:F2}' -f $sc)) { $fail += "scale $sc : hud log '$hud' does not report $sc" }
  if ($null -eq $rows[-1].help) { $fail += "scale $sc : no help ink line in the output" }
}

# Same hint text baked at two sizes: ink area scales with glyph height^2, panel height with glyph height.
$pxLo = $rows[0].px; $pxHi = $rows[-1].px
$inkRatio = $rows[0].help.ink / [double]$rows[-1].help.ink
$inkWant = [math]::Pow($pxLo / $pxHi, 2)
$hRatio = $rows[0].help.h / [double]$rows[-1].help.h
Write-Host ("help ink: {0} px in {1}x{2} at {3:F2} px vs {4} px in {5}x{6} at {7:F2} px" -f `
            $rows[0].help.ink, $rows[0].help.w, $rows[0].help.h, $pxLo, $rows[-1].help.ink, $rows[-1].help.w, $rows[-1].help.h, $pxHi)
Write-Host ("  ink ratio {0:F3} (glyph-area expectation {1:F3}), panel-height ratio {2:F3} (linear expectation {3:F3})" -f `
            $inkRatio, $inkWant, $hRatio, ($pxLo / $pxHi))
if ($inkRatio -lt $inkWant * 0.85 -or $inkRatio -gt $inkWant * 1.15) {
  $fail += "ink ratio $inkRatio is not within 15% of the glyph-area expectation $inkWant"
}
# The hint block wraps, so its height is not linear in the glyph size (fewer rows fit at the small
# size) - it must still shrink. The ink area is the linear-size evidence: outlines scale with px^2.
if ($rows[-1].help.h -le $rows[0].help.h) {
  $fail += "help panel height $($rows[-1].help.h) px at $($pxHi) px is not taller than $($rows[0].help.h) px at $($pxLo) px"
}

if ($fail) {
  Write-Host ("probe_font: FAIL ({0})" -f ($fail -join '; '))
  exit 1
}
Write-Host "probe_font: PASS"
Write-Host ("glyph px = HUD_FONT_BASE {0} x hud_font_scale (src/hud.c:21, imgui GetFontSize = FontSizeBase * FontScaleMain)" -f $BASE)
foreach ($r in $rows) { Write-Host ("  scale {0:F2} -> {1:F2} px : $($r.helpLine) ; $($r.hints)" -f $r.scale, $r.px) }
exit 0
