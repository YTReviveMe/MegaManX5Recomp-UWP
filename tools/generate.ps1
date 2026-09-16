param(
    [Parameter(Mandatory)][string]$DiscCue,
    [string]$Python,
    [int]$Jobs = 6
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$upstream = Join-Path $root 'upstream'
$framework = Join-Path $upstream 'psxrecomp-v4'
$cue = [IO.Path]::GetFullPath($DiscCue)
if (!(Test-Path -LiteralPath $cue -PathType Leaf)) { throw "CUE was not found: $cue" }
if (!$Python) { $Python = 'python' }
& (Join-Path $PSScriptRoot 'setup_upstream.ps1')

$cueText = Get-Content -LiteralPath $cue -Raw
$match = [regex]::Match($cueText, '(?im)^\s*FILE\s+"([^"]+)"\s+BINARY')
if (!$match.Success) { throw 'The CUE does not contain a quoted binary FILE entry.' }
$sourceBin = Join-Path (Split-Path -Parent $cue) $match.Groups[1].Value
if (!(Test-Path -LiteralPath $sourceBin -PathType Leaf)) { throw "BIN was not found: $sourceBin" }
$expectedSha1 = '10709231f857636b5ccd3cd9acebc91458dcb5fd'
$actualSha1 = (Get-FileHash -LiteralPath $sourceBin -Algorithm SHA1).Hash.ToLowerInvariant()
if ($actualSha1 -ne $expectedSha1) { throw "Unsupported disc SHA1 $actualSha1; expected $expectedSha1 (SLUS-01334)." }

$discDir = Join-Path $upstream 'mmx5'
New-Item -ItemType Directory -Path $discDir -Force | Out-Null
$localBin = Join-Path $discDir 'Mega Man X5 (USA).bin'
$localCue = Join-Path $discDir 'Mega Man X5 (USA).cue'
if (!(Test-Path -LiteralPath $localBin)) {
    try { New-Item -ItemType HardLink -Path $localBin -Target $sourceBin | Out-Null }
    catch { Copy-Item -LiteralPath $sourceBin -Destination $localBin }
}
$normalizedCue = $cueText -replace '(?im)^(\s*FILE\s+)"[^"]+"(\s+BINARY)', '$1"Mega Man X5 (USA).bin"$2'
Set-Content -LiteralPath $localCue -Value $normalizedCue -Encoding utf8NoBOM

$recompilerBuild = Join-Path $root 'build\recompiler'
& (Join-Path $PSScriptRoot 'with-msvc.ps1') cmake -S (Join-Path $framework 'recompiler') -B $recompilerBuild -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'Recompiler configuration failed.' }
& (Join-Path $PSScriptRoot 'with-msvc.ps1') cmake --build $recompilerBuild --target psxrecomp-game psxrecomp-bios -j $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Recompiler build failed.' }
$gameTool = Join-Path $recompilerBuild 'psxrecomp-game.exe'
$biosTool = Join-Path $recompilerBuild 'psxrecomp-bios.exe'

Push-Location $upstream
try {
    & $Python (Join-Path $framework 'tools\extract_psx_exe.py') $localBin 'SLUS_013.34' (Join-Path $discDir 'SLUS_013.34')
    if ($LASTEXITCODE -ne 0) { throw 'Boot executable extraction failed.' }
    & $gameTool --config (Join-Path $upstream 'game.toml')
    if ($LASTEXITCODE -ne 0) { throw 'Base game recompilation failed.' }
} finally { Pop-Location }

Push-Location $framework
try {
    & $biosTool --config (Join-Path $framework 'bios\OpenBIOS.toml')
    if ($LASTEXITCODE -ne 0) { throw 'OpenBIOS recompilation failed.' }
} finally { Pop-Location }

$aotRoot = Join-Path $root 'build\aot-static'
New-Item -ItemType Directory -Path $aotRoot -Force | Out-Null
Push-Location $upstream
try {
    & $Python (Join-Path $framework 'tools\aot_overlay_pipeline.py') extract --profile (Join-Path $upstream 'aot\overlays.json') --game-toml (Join-Path $upstream 'game.toml') --recompiler $gameTool --work-dir $aotRoot --workers $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'Original-disc overlay extraction failed.' }
} finally { Pop-Location }
$evidence = Get-ChildItem -LiteralPath $aotRoot -Directory | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (!$evidence) { throw 'Overlay evidence directory was not produced.' }
$records = @()
Get-ChildItem -LiteralPath (Join-Path $evidence.FullName 'runtime-inputs') -Filter '*.json' | Sort-Object Name | ForEach-Object {
    $records += @(Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json)
}
$captures = Join-Path $evidence.FullName 'all-captures.json'
$records | ConvertTo-Json -Depth 100 -Compress | Set-Content -LiteralPath $captures -Encoding utf8NoBOM
& $Python (Join-Path $framework 'tools\compile_overlays.py') --captures $captures --game-toml (Join-Path $upstream 'game.toml') --project-root $upstream --recompiler $gameTool --runtime-include (Join-Path $framework 'runtime\include') --out-dir (Join-Path $upstream 'generated') --static --cps --force --jobs $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Static overlay recompilation failed.' }
Write-Output "Generated base game, OpenBIOS, and static overlays from $cue"
