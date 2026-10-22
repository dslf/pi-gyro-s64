# bmp2png.ps1 -- convert a 24-bit BMP to PNG for visual review (lab helper, lab/probes)
param([string]$InPath, [string]$OutPath)
Add-Type -AssemblyName PresentationCore
$bytes = [System.IO.File]::ReadAllBytes($InPath)
$off = [BitConverter]::ToInt32($bytes, 10)
$w = [BitConverter]::ToInt32($bytes, 18)
$h = [BitConverter]::ToInt32($bytes, 22)
$stride = [int]((($w * 3) + 3) -band -4)
$img = New-Object System.Windows.Media.Imaging.WriteableBitmap($w, $h, 96, 96, [System.Windows.Media.PixelFormats]::Bgr32, $null)
for ($y = 0; $y -lt $h; $y++) {
  $src = $bytes[($off + ($h - 1 - $y) * $stride) .. ($off + ($h - 1 - $y) * $stride + $w * 3 - 1)]
  $row = New-Object byte[] ($w * 4)
  for ($x = 0; $x -lt $w; $x++) {
    $row[$x * 4] = $src[$x * 3]
    $row[$x * 4 + 1] = $src[$x * 3 + 1]
    $row[$x * 4 + 2] = $src[$x * 3 + 2]
    $row[$x * 4 + 3] = 255
  }
  $img.WritePixels([System.Windows.Int32Rect]::New(0, $y, $w, 1), $row, $w * 4, 0)
}
$enc = New-Object System.Windows.Media.Imaging.PngBitmapEncoder
$enc.Frames.Add([System.Windows.Media.Imaging.BitmapFrame]::Create($img))
$fs = [System.IO.File]::Create($OutPath)
$enc.Save($fs)
$fs.Close()
Write-Output "wrote $OutPath ($($bytes.Length) byte bmp -> $((Get-Item $OutPath).Length) byte png)"
