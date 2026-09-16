param([Parameter(Mandatory=$true)][string]$PackagePath)
$ErrorActionPreference = 'Stop'
$package = Get-Item -LiteralPath $PackagePath
$certificate = Get-ChildItem Cert:\CurrentUser\My | Where-Object {
    $_.Subject -eq 'CN=Revive' -and $_.FriendlyName -eq 'Mega Man X5 Recomp UWP' -and
    $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date).AddDays(30)
} | Sort-Object NotAfter | Select-Object -First 1
if (!$certificate) {
    $certificate = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=Revive' -FriendlyName 'Mega Man X5 Recomp UWP' `
        -CertStoreLocation Cert:\CurrentUser\My -KeyExportPolicy Exportable -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears(2)
}
$certificate.FriendlyName = 'Mega Man X5 Recomp UWP'
$signTool = Get-ChildItem -LiteralPath (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin') -Recurse -Filter SignTool.exe |
    Where-Object FullName -Match '\\x64\\SignTool\.exe$' | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
& $signTool sign /fd SHA256 /sha1 $certificate.Thumbprint /s My /v $package.FullName
if ($LASTEXITCODE -ne 0) { throw "SignTool failed with exit code $LASTEXITCODE" }
Write-Output "Signed package: $($package.FullName)"
