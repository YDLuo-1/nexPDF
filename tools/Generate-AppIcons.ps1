param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot "..\app\assets")
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null

function New-RoundedRectanglePath([System.Drawing.RectangleF]$Rectangle, [float]$Radius) {
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $diameter = $Radius * 2
    $path.AddArc($Rectangle.X, $Rectangle.Y, $diameter, $diameter, 180, 90)
    $path.AddArc($Rectangle.Right - $diameter, $Rectangle.Y, $diameter, $diameter, 270, 90)
    $path.AddArc($Rectangle.Right - $diameter, $Rectangle.Bottom - $diameter, $diameter, $diameter, 0, 90)
    $path.AddArc($Rectangle.X, $Rectangle.Bottom - $diameter, $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    return $path
}

function New-AppIconBitmap([int]$Size) {
    $bitmap = [System.Drawing.Bitmap]::new($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $scale = $Size / 256.0
    $graphics.ScaleTransform($scale, $scale)

    $backgroundPath = New-RoundedRectanglePath ([System.Drawing.RectangleF]::new(12, 12, 232, 232)) 54
    $backgroundBrush = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        [System.Drawing.PointF]::new(36, 24), [System.Drawing.PointF]::new(220, 232),
        [System.Drawing.Color]::FromArgb(255, 51, 70, 168), [System.Drawing.Color]::FromArgb(255, 24, 35, 79))
    $graphics.FillPath($backgroundBrush, $backgroundPath)

    $shadowPath = New-RoundedRectanglePath ([System.Drawing.RectangleF]::new(67, 37, 134, 197)) 13
    $shadowBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(56, 8, 13, 35))
    $graphics.FillPath($shadowBrush, $shadowPath)

    $pagePath = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $pagePath.AddLine(76, 28, 153, 28)
    $pagePath.AddLine(153, 28, 195, 70)
    $pagePath.AddLine(195, 70, 195, 213)
    $pagePath.AddArc(165, 198, 30, 30, 0, 90)
    $pagePath.AddLine(180, 228, 76, 228)
    $pagePath.AddArc(61, 198, 30, 30, 90, 90)
    $pagePath.AddLine(61, 213, 61, 43)
    $pagePath.AddArc(61, 28, 30, 30, 180, 90)
    $pagePath.CloseFigure()
    $pageBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 248, 250, 255))
    $graphics.FillPath($pageBrush, $pagePath)

    $foldPath = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $foldPath.AddPolygon([System.Drawing.PointF[]]@(
        [System.Drawing.PointF]::new(153, 28),
        [System.Drawing.PointF]::new(195, 70),
        [System.Drawing.PointF]::new(166, 70),
        [System.Drawing.PointF]::new(153, 57)))
    $foldBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 199, 208, 255))
    $graphics.FillPath($foldBrush, $foldPath)

    $accentPen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 232, 72, 86), 19)
    $accentPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $accentPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $accentPen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $graphics.DrawLines($accentPen, [System.Drawing.PointF[]]@(
        [System.Drawing.PointF]::new(91, 177),
        [System.Drawing.PointF]::new(91, 91),
        [System.Drawing.PointF]::new(165, 177),
        [System.Drawing.PointF]::new(165, 91)))

    $accentPen.Dispose()
    $foldBrush.Dispose()
    $pageBrush.Dispose()
    $pagePath.Dispose()
    $shadowBrush.Dispose()
    $shadowPath.Dispose()
    $backgroundBrush.Dispose()
    $backgroundPath.Dispose()
    $graphics.Dispose()
    return $bitmap
}

function Get-PngBytes([int]$Size) {
    $bitmap = New-AppIconBitmap $Size
    $stream = [System.IO.MemoryStream]::new()
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    $bytes = $stream.ToArray()
    $stream.Dispose()
    return $bytes
}

function Write-Ico([string]$Path, [int[]]$Sizes) {
    $images = @($Sizes | ForEach-Object { ,(Get-PngBytes $_) })
    $stream = [System.IO.File]::Create($Path)
    $writer = [System.IO.BinaryWriter]::new($stream)
    $writer.Write([uint16]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]$Sizes.Count)
    $offset = 6 + 16 * $Sizes.Count
    for ($index = 0; $index -lt $Sizes.Count; $index++) {
        $size = $Sizes[$index]
        $writer.Write([byte]($(if ($size -ge 256) { 0 } else { $size })))
        $writer.Write([byte]($(if ($size -ge 256) { 0 } else { $size })))
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]$images[$index].Length)
        $writer.Write([uint32]$offset)
        $offset += $images[$index].Length
    }
    foreach ($image in $images) { $writer.Write([byte[]]$image) }
    $writer.Dispose()
    $stream.Dispose()
}

function Get-BigEndianUInt32([uint32]$Value) {
    $bytes = [System.BitConverter]::GetBytes($Value)
    [array]::Reverse($bytes)
    return $bytes
}

function Write-Icns([string]$Path) {
    $chunks = @(
        @{ Type = "icp4"; Size = 16 }, @{ Type = "icp5"; Size = 32 },
        @{ Type = "icp6"; Size = 64 }, @{ Type = "ic07"; Size = 128 },
        @{ Type = "ic08"; Size = 256 }, @{ Type = "ic09"; Size = 512 },
        @{ Type = "ic10"; Size = 1024 }
    ) | ForEach-Object { @{ Type = $_.Type; Data = Get-PngBytes $_.Size } }
    $totalLength = 8 + ($chunks | ForEach-Object { 8 + $_.Data.Length } | Measure-Object -Sum).Sum
    $stream = [System.IO.File]::Create($Path)
    $header = [System.Text.Encoding]::ASCII.GetBytes("icns")
    $stream.Write($header, 0, $header.Length)
    $lengthBytes = Get-BigEndianUInt32 ([uint32]$totalLength)
    $stream.Write($lengthBytes, 0, $lengthBytes.Length)
    foreach ($chunk in $chunks) {
        $typeBytes = [System.Text.Encoding]::ASCII.GetBytes($chunk.Type)
        $stream.Write($typeBytes, 0, $typeBytes.Length)
        $chunkLengthBytes = Get-BigEndianUInt32 ([uint32](8 + $chunk.Data.Length))
        $stream.Write($chunkLengthBytes, 0, $chunkLengthBytes.Length)
        $stream.Write([byte[]]$chunk.Data, 0, $chunk.Data.Length)
    }
    $stream.Dispose()
}

$pngPath = Join-Path $OutputDirectory "nexpdf-256.png"
$bitmap = New-AppIconBitmap 256
$bitmap.Save($pngPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()
Write-Ico (Join-Path $OutputDirectory "nexpdf.ico") @(16, 24, 32, 48, 64, 128, 256)
Write-Icns (Join-Path $OutputDirectory "nexpdf.icns")
Write-Host "Generated nexPDF PNG, ICO and ICNS assets in $OutputDirectory"
