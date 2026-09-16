param([string]$BuildDirectory, [string]$OutputPath)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (!$BuildDirectory) { $BuildDirectory = Join-Path $root 'build\uwp' }
if (!$OutputPath) { $OutputPath = Join-Path $BuildDirectory 'MegaManX5RecompUWP.appx' }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
if (!(Test-Path -LiteralPath $BuildDirectory -PathType Container)) { throw "Build directory does not exist: $BuildDirectory" }
$files = @('MegaManX5.exe','MegaManX5.winmd','SDL2.dll','libuwp.dll','opengl32.dll','libgallium_wgl.dll','dxil.dll','z-1.dll','game.toml')
foreach ($file in $files) {
    if (!(Test-Path -LiteralPath (Join-Path $BuildDirectory $file) -PathType Leaf)) { throw "Missing package file: $file" }
}
$assetDirectory = Join-Path $BuildDirectory 'uwp-assets'
& (Join-Path $PSScriptRoot 'generate_uwp_visual_assets.ps1') -OutputDirectory $assetDirectory
$makeAppx = Get-ChildItem -LiteralPath (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin') -Recurse -Filter MakeAppx.exe |
    Where-Object FullName -Match '\\x64\\MakeAppx\.exe$' | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (!$makeAppx) { throw 'MakeAppx.exe was not found.' }
$stage = Join-Path $BuildDirectory ('.uwp-stage-' + [Guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $stage | Out-Null
    foreach ($file in $files) { Copy-Item -LiteralPath (Join-Path $BuildDirectory $file) -Destination $stage }
    Copy-Item -LiteralPath (Join-Path $BuildDirectory 'bios') -Destination (Join-Path $stage 'bios') -Recurse
    if (Test-Path -LiteralPath (Join-Path $BuildDirectory 'mods')) { Copy-Item -LiteralPath (Join-Path $BuildDirectory 'mods') -Destination (Join-Path $stage 'mods') -Recurse }
    New-Item -ItemType Directory -Path (Join-Path $stage 'assets') | Out-Null
    Copy-Item -Path (Join-Path $assetDirectory '*.png') -Destination (Join-Path $stage 'assets')
    Copy-Item -LiteralPath (Join-Path $root 'uwp\Package.appxmanifest') -Destination (Join-Path $stage 'AppxManifest.xml')
    & $makeAppx pack /d $stage /p $OutputPath /o
    if ($LASTEXITCODE -ne 0) { throw "MakeAppx failed with exit code $LASTEXITCODE" }
} finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
Write-Output "Created package: $OutputPath"

$vclibs = Get-ChildItem -LiteralPath (Join-Path ${env:ProgramFiles(x86)} 'Microsoft SDKs\Windows Kits\10\ExtensionSDKs\Microsoft.VCLibs\14.0\Appx\Retail\x64') -Filter 'Microsoft.VCLibs.x64.14.00.appx' -File -ErrorAction SilentlyContinue | Select-Object -First 1
if ($vclibs) {
    $dependencyDirectory = Join-Path (Split-Path -Parent $OutputPath) 'Dependencies\x64'
    New-Item -ItemType Directory -Path $dependencyDirectory -Force | Out-Null
    Copy-Item -LiteralPath $vclibs.FullName -Destination $dependencyDirectory -Force
    Write-Output "Staged Xbox dependency: $(Join-Path $dependencyDirectory $vclibs.Name)"
} else {
    Write-Warning 'Microsoft.VCLibs.x64.14.00.appx was not found in the Windows SDK.'
}
