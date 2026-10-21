# Draws the icon of the port (tools\package\lionking.ico) from scratch with System.Drawing:
# a setting sun over a rocky outcrop on a dusk sky. Original artwork, nothing is taken from the
# game files. Sizes 16, 24, 32, 48, 64, 128, 256, stored as PNG entries (Windows Vista and later).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\package\make_icon.ps1 [OUT.ico]
param([string]$Out = (Join-Path $PSScriptRoot 'lionking.ico'))
Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = 'Stop'

function Draw-Icon([int]$n) {
	$bmp = New-Object System.Drawing.Bitmap $n, $n, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
	$g = [System.Drawing.Graphics]::FromImage($bmp)
	$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
	$g.Clear([System.Drawing.Color]::Transparent)
	$s = $n / 256.0
	# rounded square background: dusk gradient, purple at the top, orange at the horizon
	$r = [single](40 * $s); $w = [single]($n - 1)
	$path = New-Object System.Drawing.Drawing2D.GraphicsPath
	$path.AddArc(0, 0, 2 * $r, 2 * $r, 180, 90)
	$path.AddArc($w - 2 * $r, 0, 2 * $r, 2 * $r, 270, 90)
	$path.AddArc($w - 2 * $r, $w - 2 * $r, 2 * $r, 2 * $r, 0, 90)
	$path.AddArc(0, $w - 2 * $r, 2 * $r, 2 * $r, 90, 90)
	$path.CloseFigure()
	$sky = New-Object System.Drawing.Drawing2D.LinearGradientBrush (New-Object System.Drawing.PointF 0, 0), (New-Object System.Drawing.PointF 0, $n), ([System.Drawing.Color]::FromArgb(255, 74, 32, 92)), ([System.Drawing.Color]::FromArgb(255, 250, 140, 40))
	$g.FillPath($sky, $path)
	$g.SetClip($path)
	# the sun: a large disc with a soft glow, its centre a little below the middle
	$cx = 128 * $s; $cy = 150 * $s
	foreach ($k in @(@(96, 40), @(82, 70))) {
		$rr = $k[0] * $s
		$glow = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb($k[1], 255, 230, 120))
		$g.FillEllipse($glow, [single]($cx - $rr), [single]($cy - $rr), [single](2 * $rr), [single](2 * $rr))
	}
	$rr = 66 * $s
	$sun = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 255, 214, 80))
	$g.FillEllipse($sun, [single]($cx - $rr), [single]($cy - $rr), [single](2 * $rr), [single](2 * $rr))
	# the outcrop: a dark slab jutting out to the right, over a strip of savanna
	$rock = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 40, 22, 30))
	$pts = @(@(0, 256), @(0, 196), @(40, 176), @(92, 168), @(150, 150), @(214, 140), @(226, 148),
		@(170, 172), @(150, 196), @(140, 256)) | ForEach-Object { New-Object System.Drawing.PointF ([single]($_[0] * $s)), ([single]($_[1] * $s)) }
	$g.FillPolygon($rock, [System.Drawing.PointF[]]$pts)
	$g.FillRectangle($rock, 0, [single](212 * $s), [single]$n, [single](44 * $s))
	$g.ResetClip()
	$g.Dispose()
	$ms = New-Object System.IO.MemoryStream
	$bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
	$bmp.Dispose()
	return , $ms.ToArray()
}

$sizes = 16, 24, 32, 48, 64, 128, 256
$images = @($sizes | ForEach-Object { , (Draw-Icon $_) })
$f = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $f
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$sizes.Count)  # ICONDIR
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {  # ICONDIRENTRY: 256 is stored as 0
	$d = if ($sizes[$i] -ge 256) { 0 } else { $sizes[$i] }
	$bw.Write([byte]$d); $bw.Write([byte]$d); $bw.Write([byte]0); $bw.Write([byte]0)
	$bw.Write([uint16]1); $bw.Write([uint16]32)
	$bw.Write([uint32]$images[$i].Length); $bw.Write([uint32]$offset)
	$offset += $images[$i].Length
}
foreach ($img in $images) { $bw.Write([byte[]]$img) }
$bw.Flush()
[System.IO.File]::WriteAllBytes($Out, $f.ToArray())
Write-Host "icon: $Out ($($f.Length) bytes)"
