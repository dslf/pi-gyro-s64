$fails = 0
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location (Split-Path -Parent $here)

# Every frame the suite produces lands in tests/out/ (git-ignored), never in the repo root.
$Out = Join-Path $here "out"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
Get-ChildItem $Out -Filter *.bmp -ErrorAction SilentlyContinue | Remove-Item -Force

function Fail($msg) { $script:fails++; Write-Output "FAIL $msg" }
function Ok($msg)   { Write-Output "ok   $msg" }

# Run viewer.exe (repo root, git-ignored) with a verbatim argument string; capture stdout+stderr and exit code.
# The suite runs from the repo root: assets/ and shaders/ resolve relative to the current directory.
function Run-Viewer([string]$cmdline) {
  $so = [System.IO.Path]::GetTempFileName()
  $se = [System.IO.Path]::GetTempFileName()
  $p = Start-Process -FilePath ".\viewer.exe" -ArgumentList $cmdline -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput $so -RedirectStandardError $se
  $out = ((Get-Content $so -Raw) + (Get-Content $se -Raw)).Trim()
  Remove-Item $so, $se -Force -ErrorAction SilentlyContinue
  return @{ code = $p.ExitCode; out = $out }
}

# --- syntax check -----------------------------------------------------------
$syn = gcc -c -std=c11 -Wall -Wextra -fsyntax-only src/main.c src/gl.c src/vmath.c src/camera.c src/obj.c src/mesh.c src/shader.c src/render.c src/img.c 2>&1
if ($LASTEXITCODE -ne 0) { Fail "strict syntax check"; Write-Output ($syn | Out-String) } else { Ok "strict -Wall -Wextra syntax" }

# --- build ------------------------------------------------------------------
make 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { Fail "build"; Write-Output "exit $LASTEXITCODE"; exit 1 }
Ok "build"

# --- selftest ---------------------------------------------------------------
$t = Run-Viewer "--selftest"
if ($t.code -ne 0) { Fail "selftest"; Write-Output $t.out } else { Ok "selftest" }

# --- headless renders -------------------------------------------------------
foreach ($r in @("--render assets/cube.obj $Out/out_cube.bmp 256 256", "--render assets/utah_teapot.obj $Out/out_teapot.bmp 800 600", "--render assets/cube.obj $Out/out_cube_wire.bmp 256 256 --wire", "--render assets/utah_teapot.obj $Out/out_teapot_ortho.bmp 400 300 --ortho", "--render assets/utah_teapot.obj $Out/out_teapot_fov80.bmp 400 300 --fov 80", "--render assets/cube.obj $Out/out_cube_tall.bmp 100 400")) {
  $r2 = Run-Viewer $r
  if ($r2.code -ne 0) { Fail "render '$r'"; Write-Output $r2.out } else { Ok "render $r" }
}

foreach ($c in @(@{ a = "--render does_not_exist.obj $Out/rejected.bmp"; want = 2 }, @{ a = "--render assets/cube.obj $Out/rejected.bmp 2 2"; want = 2 }, @{ a = "--render assets/cube.obj"; want = 2 }, @{ a = "--render assets/cube.obj $Out/rejected.bmp 100 100 --fov 120"; want = 2 }, @{ a = "--render assets/cube.obj $Out/t_fov15.bmp 100 100 --ortho --fov 15"; want = 0 }, @{ a = "--selftest --ortho"; want = 0 })) {
  $r = Run-Viewer $c.a
  if ($r.code -ne $c.want) { Fail "exit $($c.want) expected for '$($c.a)' (got $($r.code))" } else { Ok "exit $($r.code) for '$($c.a)'" }
}

# --- image checks -----------------------------------------------------------
python tests/check_images.py image "$Out/out_cube.bmp" cube
if ($LASTEXITCODE -ne 0) { Fail "cube image checks" }
python tests/check_images.py image "$Out/out_teapot.bmp" teapot
if ($LASTEXITCODE -ne 0) { Fail "teapot image checks" }
python tests/check_images.py image "$Out/out_teapot_ortho.bmp" teapot
if ($LASTEXITCODE -ne 0) { Fail "orthographic teapot checks" }
python tests/check_images.py image "$Out/out_teapot_fov80.bmp" teapot
if ($LASTEXITCODE -ne 0) { Fail "wide fov teapot checks" }
python tests/check_images.py framing "$Out/out_cube_tall.bmp"
if ($LASTEXITCODE -ne 0) { Fail "extreme aspect framing checks" }

# perspective must be visible: ortho keeps proportions, wider fov foreshortens more.
# Same cube at the same camera angles under four projections - the ordering of the
# silhouette taper is the proof, not the teapot's own shape.
foreach ($f in @(15, 45, 80)) {
  $r = Run-Viewer "--render --yaw 30 --pitch 20 --fov $f assets/cube.obj $Out/out_cube_f$f.bmp 200 200"
  if ($r.code -ne 0) { Fail "render fov $f"; Write-Output $r.out } else { Ok "render cube fov $f" }
}
$ro = Run-Viewer "--render --ortho --yaw 30 --pitch 20 --fov 45 assets/cube.obj $Out/out_cube_ortho.bmp 200 200"
if ($ro.code -ne 0) { Fail "render cube ortho"; Write-Output $ro.out } else { Ok "render cube ortho" }
python tests/check_images.py projection "$Out/out_cube_ortho.bmp" "$Out/out_cube_f15.bmp" "$Out/out_cube_f45.bmp" "$Out/out_cube_f80.bmp"
if ($LASTEXITCODE -ne 0) { Fail "projection comparison" }

# wireframe must cover far less than the shaded cube
$py = @'
from PIL import Image
import numpy as np
import sys
def cov(p):
    a = np.asarray(Image.open(p)).astype(int)
    return np.abs(a - [128, 133, 143]).sum(axis=2).mean()
w, s = cov(sys.argv[1]), cov(sys.argv[2])
print('ok   wireframe %.1f%% < shaded %.1f%%' % (w * 100, s * 100) if w < s * 0.5 else 'FAIL wire %.1f%% vs shaded %.1f%%' % (w * 100, s * 100))
sys.exit(0 if w < s * 0.5 else 1)
'@
$py | python - "$Out/out_cube_wire.bmp" "$Out/out_cube.bmp"
if ($LASTEXITCODE -ne 0) { Fail "wireframe coverage comparison" } else { Ok "wireframe covers less than shaded" }

# --- screenshot path --------------------------------------------------------
Remove-Item -Force "$Out/shot.bmp" -ErrorAction SilentlyContinue
$r = Run-Viewer "--render assets/utah_teapot.obj $Out/shot.bmp 640 480"
if ($r.code -ne 0) { Fail "screenshot render" } else { Ok "offscreen frame can be written to disk" }

if ($fails -eq 0) { Write-Output "tests: PASS"; exit 0 }
Write-Output "tests: $fails FAILED"
exit 1
