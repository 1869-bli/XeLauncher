param(
  [string]$Screenshot = "C:\Users\fifi\Documents\ShareX\Screenshots\2026-07\xenia-config-editor_BbtPag9JIn.png"
)
Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = "Stop"

$dir    = Split-Path -Parent $MyInvocation.MyCommand.Path
$imgdir = Join-Path $dir "images"
New-Item -ItemType Directory -Force -Path $imgdir | Out-Null

$amber   = [System.Drawing.Color]::FromArgb(255, 182, 39)
$dark    = [System.Drawing.Color]::FromArgb(30, 30, 30)
$white   = [System.Drawing.Color]::FromArgb(255, 255, 255)
$bannerL = [System.Drawing.Color]::FromArgb(247, 247, 247)
$bannerR = [System.Drawing.Color]::FromArgb(253, 233, 196)
$panelT  = [System.Drawing.Color]::FromArgb(252, 252, 252)
$panelB  = [System.Drawing.Color]::FromArgb(246, 239, 226)

function New-RoundedRectPath([int]$x, [int]$y, [int]$w, [int]$h, [int]$r) {
  $p = New-Object System.Drawing.Drawing2D.GraphicsPath
  $d = $r * 2
  $p.AddArc($x, $y, $d, $d, 180, 90)
  $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
  $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
  $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
  $p.CloseFigure()
  return $p
}

function Draw-XMonogram($g, [float]$x, [float]$y, [float]$size) {
  $path = New-RoundedRectPath ([int]$x) ([int]$y) ([int]$size) ([int]$size) ([int]($size * 0.24))
  $brush = New-Object System.Drawing.SolidBrush($dark)
  $g.FillPath($brush, $path)
  $brush.Dispose()
  $path.Dispose()

  $pen = New-Object System.Drawing.Pen($amber, ($size * 0.19))
  $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
  $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
  $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
  $inner = $size * 0.24
  $g.DrawLine($pen, $x + $inner, $y + $inner, $x + $size - $inner, $y + $size - $inner)
  $g.DrawLine($pen, $x + $size - $inner, $y + $inner, $x + $inner, $y + $size - $inner)
  $pen.Dispose()
}

function Save-Bmp24($bmp, [string]$path) {
  $out = New-Object System.Drawing.Bitmap($bmp.Width, $bmp.Height, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
  $g = [System.Drawing.Graphics]::FromImage($out)
  $g.DrawImage($bmp, 0, 0, $bmp.Width, $bmp.Height)
  $g.Dispose()
  $out.Save($path, [System.Drawing.Imaging.ImageFormat]::Bmp)
  $out.Dispose()
}

# --- logo source (256px, transparent) ---
$logo = New-Object System.Drawing.Bitmap(256, 256, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$lg = [System.Drawing.Graphics]::FromImage($logo)
$lg.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$lg.Clear([System.Drawing.Color]::Transparent)
Draw-XMonogram $lg 0 0 256
$lg.Dispose()
$logo.Save((Join-Path $imgdir "logo.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$logo.Dispose()

# --- app.ico (multi-size uncompressed DIB entries, written next to the project root) ---
function New-BmpIconEntry([int]$size) {
  $bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $g.Clear([System.Drawing.Color]::Transparent)
  Draw-XMonogram $g 0 0 $size
  $g.Dispose()

  $rect = New-Object System.Drawing.Rectangle(0, 0, $size, $size)
  $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, $bmp.PixelFormat)
  $byteCount = $data.Stride * $size
  $raw = New-Object byte[] $byteCount
  [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $raw, 0, $byteCount)
  $bmp.UnlockBits($data)
  $bmp.Dispose()

  $maskRow = [int]([math]::Ceiling($size / 32.0) * 4)
  $ms = New-Object System.IO.MemoryStream
  $bw = New-Object System.IO.BinaryWriter($ms)
  $bw.Write([int32]40); $bw.Write([int32]$size); $bw.Write([int32]($size * 2))
  $bw.Write([int16]1); $bw.Write([int16]32); $bw.Write([int32]0)
  $bw.Write([int32]($size * $size * 4)); $bw.Write([int32]0); $bw.Write([int32]0)
  $bw.Write([int32]0); $bw.Write([int32]0)
  for ($y = $size - 1; $y -ge 0; $y--) {
    $bw.Write($raw, $y * $data.Stride, $size * 4)
  }
  $zeroRow = New-Object byte[] $maskRow
  for ($y = 0; $y -lt $size; $y++) { $bw.Write($zeroRow) }
  $bw.Flush()
  $dib = $ms.ToArray()
  $bw.Close(); $ms.Dispose()
  return , $dib
}

$sizes = 16, 24, 32, 48, 64, 128, 256
$iconData = @()
foreach ($s in $sizes) { $iconData += , (New-BmpIconEntry $s) }

$root = Split-Path $dir -Parent
$icoPath = Join-Path $root "app.ico"
$fs = New-Object System.IO.FileStream($icoPath, [System.IO.FileMode]::Create)
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
  $s = $sizes[$i]; $d = $iconData[$i]
  $w = if ($s -ge 256) { 0 } else { $s }
  $bw.Write([byte]$w); $bw.Write([byte]$w)
  $bw.Write([byte]0); $bw.Write([byte]0)
  $bw.Write([uint16]1); $bw.Write([uint16]32)
  $bw.Write([uint32]$d.Length); $bw.Write([uint32]$offset)
  $offset += $d.Length
}
foreach ($d in $iconData) { $bw.Write($d) }
$bw.Close(); $fs.Close()
Write-Output "app.ico written to $root"

# --- banner 493x58 ---
$banner = New-Object System.Drawing.Bitmap(493, 58)
$g = [System.Drawing.Graphics]::FromImage($banner)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.Clear($bannerL)
$grad = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
  (New-Object System.Drawing.Rectangle(0, 0, 493, 58)), $bannerL, $bannerR, 0)
$g.FillRectangle($grad, 0, 0, 493, 58)
$grad.Dispose()
$ac = New-Object System.Drawing.SolidBrush($amber)
$g.FillRectangle($ac, 0, 56, 493, 2)
$ac.Dispose()
Draw-XMonogram $g 433 5 48
$g.Dispose()
Save-Bmp24 $banner (Join-Path $imgdir "banner.bmp")
$banner.Dispose()

# --- dialog background 493x312 ---
$dialog = New-Object System.Drawing.Bitmap(493, 312)
$g = [System.Drawing.Graphics]::FromImage($dialog)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$grad = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
  (New-Object System.Drawing.Rectangle(0, 0, 493, 312)), $panelT, $panelB, 90)
$g.FillRectangle($grad, 0, 0, 493, 312)
$grad.Dispose()

$src = [System.Drawing.Image]::FromFile($Screenshot)
$panelW = [int](493 * 0.365)
$scaledW = [int]($src.Width * 312 / $src.Height)
$sx = [int](($scaledW - $panelW) / 2)
$srcRect = New-Object System.Drawing.Rectangle($sx, 0, $panelW, 312)
$dstRect = New-Object System.Drawing.Rectangle(0, 0, $panelW, 312)
$g.DrawImage($src, $dstRect, $srcRect, [System.Drawing.GraphicsUnit]::Pixel)
$src.Dispose()

$ac = New-Object System.Drawing.SolidBrush($amber)
$g.FillRectangle($ac, $panelW - 2, 0, 2, 312)
$ac.Dispose()

$stripH = 56
$sy = 312 - $stripH
$strip = New-Object System.Drawing.SolidBrush($dark)
$g.FillRectangle($strip, 0, $sy, 493, $stripH)
$strip.Dispose()
Draw-XMonogram $g 14 ($sy + 10) 36
$font = New-Object System.Drawing.Font("Segoe UI", 13, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
$txt = New-Object System.Drawing.SolidBrush($white)
$sf = New-Object System.Drawing.StringFormat
$sf.LineAlignment = [System.Drawing.StringAlignment]::Center
$tx = 14 + 36 + 12
$tw = 493 - 14 - 36 - 12
$txtRect = New-Object System.Drawing.RectangleF -ArgumentList @($tx, $sy, $tw, $stripH)
$g.DrawString("Xenia Config Editor", $font, $txt, $txtRect, $sf)
$sf.Dispose(); $txt.Dispose(); $font.Dispose()
$g.Dispose()
Save-Bmp24 $dialog (Join-Path $imgdir "dialog.bmp")
$dialog.Dispose()

Write-Output "assets written to $imgdir"
