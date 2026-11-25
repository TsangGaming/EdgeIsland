[CmdletBinding()]
param([Parameter(Mandatory)][string[]]$Files, [Parameter(Mandatory)][string]$ExpectedPublisher)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ExpectedPublisher)) { throw 'Set the expected signer common name from the approved SignPath policy.' }
foreach ($file in $Files) {
    $item = Get-Item -LiteralPath $file
    $signature = Get-AuthenticodeSignature -LiteralPath $item.FullName
    if ($signature.Status -ne 'Valid' -or $null -eq $signature.TimeStamperCertificate) {
        throw "Windows could not verify an Authenticode signature with a timestamp on $($item.Name): $($signature.Status)"
    }
    $publisher = $signature.SignerCertificate.GetNameInfo([System.Security.Cryptography.X509Certificates.X509NameType]::SimpleName,$false)
    if ($publisher -cne $ExpectedPublisher) { throw "Unexpected publisher '$publisher' on $($item.Name)" }
    if ($item.VersionInfo.ProductName -cne 'Edge Island') { throw "Unexpected product on $($item.Name)" }
    Write-Host "$($item.Name) — $publisher — Valid — timestamp present"
}
