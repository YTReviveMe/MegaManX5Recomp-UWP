param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null

$root = Split-Path -Parent $PSScriptRoot
$sourcePath = Join-Path $root 'upstream\recomp\launcher\boxart.tga'
if (!(Test-Path -LiteralPath $sourcePath -PathType Leaf)) { throw "Missing Mega Man X5 artwork: $sourcePath" }
$bytes = [IO.File]::ReadAllBytes($sourcePath)
$sourceWidth = [BitConverter]::ToUInt16($bytes, 12)
$sourceHeight = [BitConverter]::ToUInt16($bytes, 14)
if ($bytes[2] -ne 2 -or $bytes[16] -ne 24) { throw 'Expected an uncompressed 24-bit TGA boxart image.' }
$source = [Drawing.Bitmap]::new($sourceWidth, $sourceHeight, [Drawing.Imaging.PixelFormat]::Format24bppRgb)
$sourceRect = [Drawing.Rectangle]::new(0, 0, $sourceWidth, $sourceHeight)
$sourceData = $source.LockBits($sourceRect, [Drawing.Imaging.ImageLockMode]::WriteOnly, [Drawing.Imaging.PixelFormat]::Format24bppRgb)
try { [Runtime.InteropServices.Marshal]::Copy($bytes, 18 + $bytes[0], $sourceData.Scan0, $sourceWidth * $sourceHeight * 3) }
finally { $source.UnlockBits($sourceData) }

function New-Canvas([int]$Width, [int]$Height) {
    $bitmap = [Drawing.Bitmap]::new($Width, $Height, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.Clear([Drawing.Color]::FromArgb(255, 7, 18, 36))
    $graphics.CompositingQuality = [Drawing.Drawing2D.CompositingQuality]::HighQuality
    $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    return @($bitmap, $graphics)
}

try {
    foreach ($asset in @(@('StoreLogo.png',50), @('Square44x44Logo.png',44), @('Square150x150Logo.png',150))) {
        $size = [int]$asset[1]
        $pair = New-Canvas $size $size
        try {
            $crop = [Drawing.Rectangle]::new(82, 0, 430, 430)
            $dest = [Drawing.Rectangle]::new(0, 0, $size, $size)
            $pair[1].DrawImage($source, $dest, $crop, [Drawing.GraphicsUnit]::Pixel)
            $pen = [Drawing.Pen]::new([Drawing.Color]::FromArgb(255, 38, 150, 255), [Math]::Max(1, $size / 50))
            try { $pair[1].DrawRectangle($pen, 0, 0, $size - 1, $size - 1) } finally { $pen.Dispose() }
            $pair[0].Save((Join-Path $OutputDirectory $asset[0]), [Drawing.Imaging.ImageFormat]::Png)
        } finally { $pair[1].Dispose(); $pair[0].Dispose() }
    }

    $pair = New-Canvas 620 300
    try {
        $pair[1].DrawImage($source, [Drawing.Rectangle]::new(0, 0, 620, 300),
            [Drawing.Rectangle]::new(82, 20, 430, 208), [Drawing.GraphicsUnit]::Pixel)
        $pair[0].Save((Join-Path $OutputDirectory 'SplashScreen.png'), [Drawing.Imaging.ImageFormat]::Png)
    } finally { $pair[1].Dispose(); $pair[0].Dispose() }
} finally {
    $source.Dispose()
}
