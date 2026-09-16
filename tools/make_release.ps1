param([string]$OutputPath)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build\uwp'
if (!$OutputPath) { $OutputPath = Join-Path $build 'MegaManX5RecompUWP-v0.1.0-x64.zip' }
$required = @(
    'MegaManX5RecompUWP.appx',
    'Dependencies\x64\Microsoft.VCLibs.x64.14.00.appx'
)
foreach ($item in $required) {
    if (!(Test-Path -LiteralPath (Join-Path $build $item) -PathType Leaf)) { throw "Missing release file: $item" }
}
$stage = Join-Path $build ('.release-stage-' + [Guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path (Join-Path $stage 'Dependencies\x64') -Force | Out-Null
    foreach ($item in $required) { Copy-Item -LiteralPath (Join-Path $build $item) -Destination (Join-Path $stage $item) }
    @'
Mega Man X5 Recomp UWP

1. Open Xbox Device Portal and add MegaManX5RecompUWP.appx.
2. Add Dependencies\x64\Microsoft.VCLibs.x64.14.00.appx as a dependency if requested.
3. Launch the app and choose Internal storage or External Storage (opens E:\ when available).
4. Browse to your Mega Man X5 USA SLUS-01334 .BIN, then press PLAY MEGA MAN X5.

Keep the CUE and BIN together if your image has both. The retail game is not included in this archive.
'@ | Set-Content -LiteralPath (Join-Path $stage 'INSTALL.txt') -Encoding utf8NoBOM
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $OutputPath -CompressionLevel Optimal -Force
} finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
Write-Output "Created release archive: $OutputPath"
exit 0
