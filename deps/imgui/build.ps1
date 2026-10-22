# Build the vendored ImGui library as a static archive for the C11 viewer.
# Product code never sees C++: it includes deps/imgui/cimgui.h only.
# Output: deps/imgui/build/libcimgui.a  (git-ignored; rebuilt by `make imgui`).
param([switch]$Clean)
# Native stderr must not become a PowerShell error record; exit codes are checked explicitly.
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$inc = @('-I.', '-Iimgui', '-Iimgui/backends', '-include', 'impl_api.h')
$defs = @('-DCIMGUI_USE_GLFW', '-DCIMGUI_USE_OPENGL3')
$flags = @('-O2', '-std=c++11', '-Wall', '-fno-exceptions', '-fno-rtti')

$sources = @(
  'imgui/imgui.cpp', 'imgui/imgui_draw.cpp', 'imgui/imgui_tables.cpp',
  'imgui/imgui_widgets.cpp', 'imgui/imgui_demo.cpp',
  'cimgui.cpp', 'cimgui_impl.cpp',
  'imgui/backends/imgui_impl_glfw.cpp', 'imgui/backends/imgui_impl_opengl3.cpp'
)

$build = 'build'
New-Item -ItemType Directory -Force $build | Out-Null
if ($Clean) { Remove-Item "$build/*.o", "$build/libcimgui.a" -Force -ErrorAction SilentlyContinue }

# Rebuild an object only when its source is newer than the object.
$objects = @()
$compiled = 0
foreach ($src in $sources) {
  $obj = Join-Path $build ([System.IO.Path]::GetFileNameWithoutExtension($src) + '.o')
  if ((Test-Path $obj) -and (Get-Item $obj).LastWriteTime -ge (Get-Item $src).LastWriteTime) {
    $objects += $obj
    continue
  }
  $out = & g++ $flags $defs $inc -c $src -o $obj 2>&1 | Out-String
  if ($LASTEXITCODE -ne 0) { Write-Output "FAIL compile $src`n$out"; exit 1 }
  if ($out.Trim().Length) { Write-Output "warnings from $src`n$($out.Trim())" }
  if (-not (Test-Path $obj)) { Write-Output "FAIL no object for $src"; exit 1 }
  $objects += $obj
  $compiled++
}

$archive = Join-Path $build 'libcimgui.a'
if ($compiled -gt 0 -or -not (Test-Path $archive)) {
  Remove-Item $archive -Force -ErrorAction SilentlyContinue
  $out = & ar rc $archive @objects 2>&1 | Out-String
  if ($LASTEXITCODE -ne 0) { Write-Output "FAIL ar`n$out"; exit 1 }
}

# The archive must expose the C entry points the viewer calls.
$syms = & nm $archive 2>&1 | Out-String
$need = @('igCreateContext', 'igDestroyContext', 'igNewFrame', 'igRender', 'igGetIO_Nil', 'igBegin', 'igEnd', 'igText',
          'ImGui_ImplGlfw_InitForOpenGL', 'ImGui_ImplGlfw_NewFrame', 'ImGui_ImplGlfw_Shutdown',
          'ImGui_ImplOpenGL3_Init', 'ImGui_ImplOpenGL3_NewFrame', 'ImGui_ImplOpenGL3_Shutdown',
          'ImGui_ImplOpenGL3_RenderDrawData')
$missing = @()
foreach ($s in $need) { if ($syms -notmatch ("\b" + $s + "\b")) { $missing += $s } }
if ($missing) { Write-Output "FAIL archive symbols missing: $($missing -join ', ')"; exit 1 }

Write-Output ("imgui: OK  objects=$($objects.Count) compiled=$compiled archive=" +
  (Get-Item $archive).Length + " bytes  C symbols present=$($need.Count)")
exit 0
