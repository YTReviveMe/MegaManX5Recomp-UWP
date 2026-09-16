param([switch]$Reset)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$upstream = Join-Path $root 'upstream'

if ($Reset -and (Test-Path -LiteralPath $upstream)) {
    throw 'Reset is intentionally non-destructive. Remove upstream yourself, then run this script again.'
}

if (!(Test-Path -LiteralPath (Join-Path $upstream '.git'))) {
    git clone --branch v0.1.0-alpha --recursive https://github.com/mstan/MegaManX5Recomp.git $upstream
    if ($LASTEXITCODE -ne 0) { throw 'Could not clone MegaManX5Recomp.' }
} else {
    git -C $upstream submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { throw 'Could not initialize upstream submodules.' }
}

function Apply-TrackedPatch([string]$Repository, [string]$Patch) {
    git -C $Repository apply --check $Patch 2>$null
    if ($LASTEXITCODE -eq 0) {
        git -C $Repository apply $Patch
        if ($LASTEXITCODE -ne 0) { throw "Could not apply $Patch" }
        return
    }
    git -C $Repository apply --reverse --check $Patch 2>$null
    if ($LASTEXITCODE -ne 0) { throw "Patch does not apply cleanly: $Patch" }
}

Apply-TrackedPatch $upstream (Join-Path $root 'patches\mmx5-uwp.patch')
Apply-TrackedPatch (Join-Path $upstream 'psxrecomp-v4') (Join-Path $root 'patches\psxrecomp-uwp.patch')
Write-Output 'Pinned upstream source and UWP patches are ready.'
