# Generates res\app.ico and res\paused.ico (rounded tile with a lightning bolt).
# Run with Windows PowerShell:  powershell -ExecutionPolicy Bypass -File tools\make-icons.ps1
Add-Type -AssemblyName System.Drawing

$sizes = 16, 20, 24, 32, 40, 48, 64, 128, 256   # <= 48 as classic bitmaps, larger as PNG
$root  = Split-Path $PSScriptRoot -Parent
$out   = Join-Path $root 'res'
New-Item -ItemType Directory -Force $out | Out-Null

function New-Frame([int]$s, [System.Drawing.Color]$top, [System.Drawing.Color]$bottom) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode   = 'AntiAlias'
    $g.PixelOffsetMode = 'HighQuality'
    $g.Clear([System.Drawing.Color]::Transparent)

    # Rounded tile
    $m = [Math]::Max(0.5, $s * 0.03)
    $r = $s * 0.24
    $w = $s - 2 * $m
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc($m, $m, 2 * $r, 2 * $r, 180, 90)
    $path.AddArc($m + $w - 2 * $r, $m, 2 * $r, 2 * $r, 270, 90)
    $path.AddArc($m + $w - 2 * $r, $m + $w - 2 * $r, 2 * $r, 2 * $r, 0, 90)
    $path.AddArc($m, $m + $w - 2 * $r, 2 * $r, 2 * $r, 90, 90)
    $path.CloseFigure()
    $rect  = New-Object System.Drawing.RectangleF 0, 0, $s, $s
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush $rect, $top, $bottom, 90
    $g.FillPath($brush, $path)

    # Lightning bolt
    $pts = @(@(0.53,0.12), @(0.73,0.12), @(0.59,0.41), @(0.75,0.41), @(0.38,0.90), @(0.47,0.56), @(0.27,0.56)) | ForEach-Object {
        New-Object System.Drawing.PointF ($_[0] * $s), ($_[1] * $s)
    }
    $g.FillPolygon([System.Drawing.Brushes]::White, [System.Drawing.PointF[]]$pts)

    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    if ($s -ge 64) {
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    } else {
        # Classic DIB frame: BITMAPINFOHEADER, bottom-up BGRA pixels, then a 1bpp AND mask.
        $bw = New-Object System.IO.BinaryWriter $ms
        $bw.Write([UInt32]40); $bw.Write([Int32]$s); $bw.Write([Int32]($s * 2))
        $bw.Write([UInt16]1); $bw.Write([UInt16]32); $bw.Write([UInt32]0)
        $bw.Write([UInt32]($s * $s * 4)); $bw.Write([Int32]0); $bw.Write([Int32]0)
        $bw.Write([UInt32]0); $bw.Write([UInt32]0)
        for ($y = $s - 1; $y -ge 0; $y--) {
            for ($x = 0; $x -lt $s; $x++) {
                $c = $bmp.GetPixel($x, $y)
                $bw.Write([byte]$c.B); $bw.Write([byte]$c.G); $bw.Write([byte]$c.R); $bw.Write([byte]$c.A)
            }
        }
        $stride = [int]([Math]::Ceiling($s / 32.0) * 4)
        for ($y = $s - 1; $y -ge 0; $y--) {
            $row = New-Object byte[] $stride
            for ($x = 0; $x -lt $s; $x++) {
                if ($bmp.GetPixel($x, $y).A -eq 0) { $row[[int][Math]::Floor($x / 8)] = $row[[int][Math]::Floor($x / 8)] -bor (0x80 -shr ($x % 8)) }
            }
            $bw.Write($row)
        }
        $bw.Flush()
    }
    $bmp.Dispose()
    return ,$ms.ToArray()
}

function Write-Ico([string]$file, [int[]]$sizes, [System.Drawing.Color]$top, [System.Drawing.Color]$bottom) {
    $frames = foreach ($s in $sizes) { ,(New-Frame $s $top $bottom) }
    $fs = [System.IO.File]::Create($file)
    $bw = New-Object System.IO.BinaryWriter $fs
    $bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($i = 0; $i -lt $sizes.Count; $i++) {
        $s = $sizes[$i]; $d = if ($s -ge 256) { 0 } else { $s }
        $bw.Write([byte]$d); $bw.Write([byte]$d); $bw.Write([byte]0); $bw.Write([byte]0)
        $bw.Write([UInt16]1); $bw.Write([UInt16]32)
        $bw.Write([UInt32]$frames[$i].Length); $bw.Write([UInt32]$offset)
        $offset += $frames[$i].Length
    }
    foreach ($f in $frames) { $bw.Write($f) }
    $bw.Close()
}

# The paused icon only ever appears in the tray, so it needs small sizes only.
Write-Ico (Join-Path $out 'app.ico')    $sizes ([System.Drawing.Color]::FromArgb(255, 0x4F, 0x8B, 0xFF)) ([System.Drawing.Color]::FromArgb(255, 0x7A, 0x4D, 0xF5))
Write-Ico (Join-Path $out 'paused.ico') @(16, 20, 24, 32, 40, 48) ([System.Drawing.Color]::FromArgb(255, 0x9A, 0x9A, 0xA0)) ([System.Drawing.Color]::FromArgb(255, 0x6E, 0x6E, 0x76))
Write-Host "Icons written to $out"
