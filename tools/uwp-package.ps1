# Packs build-uwp\uwp-layout into a signed .msix for the Xbox (Dev Mode), with the framework
# package it needs, in xbox-package\<version>\. As KytyPS5's src/uwp/uwp.ps1 package step.
#
#   tools\uwp-package.ps1
#
# Install on the Xbox: Device Portal (the address Dev Home shows) > Add > the .msix, add
# Dependencies\Microsoft.VCLibs.x64.14.00.appx, Start. Then in Dev Home set the app's type to
# Game (more memory).
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

$root = Split-Path $PSScriptRoot -Parent
$layout = "$root\build-uwp\uwp-layout"
$sdkBin = 'D:\Windows Kits\10\bin\10.0.22621.0\x64'
$vclibs = 'D:\xbox ps4 emu\PS5 emu\KytyPS5-Xbox-1.1.26286.857\Dependencies\Microsoft.VCLibs.x64.14.00.appx'
$pfx = "$root\src\uwp\shadps4-xbox-signing.pfx"
$pfxPassword = 'shadps4-xbox'   # a test key, public on purpose: it vouches for nobody
$publisher = 'CN=shadPS4-xbox'

if (-not (Test-Path "$layout\shadps4.exe")) { throw "no $layout\shadps4.exe: run build-uwp.bat first" }

# Every import must exist on the Xbox, or the app does not start there.
& "$PSScriptRoot\uwp-import-check.ps1" -Binary "$layout\shadps4.exe" -Detail

# The signing key, made once: self-signed, for code signing, as the manifest's publisher.
if (-not (Test-Path $pfx)) {
    $cert = New-SelfSignedCertificate -Type Custom -Subject $publisher -KeyUsage DigitalSignature `
        -FriendlyName 'shadPS4 Xbox test key' -CertStoreLocation 'Cert:\CurrentUser\My' `
        -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
    Export-PfxCertificate -Cert $cert -FilePath $pfx `
        -Password (ConvertTo-SecureString $pfxPassword -AsPlainText -Force) | Out-Null
    Remove-Item "Cert:\CurrentUser\My\$($cert.Thumbprint)"
    "made the signing key $pfx"
}

# Each package is newer than the last (UTC time), so it installs over it and keeps the app's data.
$now = [DateTime]::UtcNow
$version = '1.0.{0}.{1}' -f ($now.Date - [DateTime]'2026-01-01').Days, ($now.Hour * 60 + $now.Minute)
$manifest = "$layout\AppxManifest.xml"
(Get-Content $manifest -Raw) -replace '(<Identity [^>]*Version=")[^"]*(")', "`${1}$version`${2}" |
    Set-Content $manifest -NoNewline

$out = "$root\xbox-package\$version"
New-Item -ItemType Directory "$out\Dependencies" -Force | Out-Null
$msix = "$out\shadPS4-Xbox_${version}_x64.msix"
& "$sdkBin\makeappx.exe" pack /o /d $layout /p $msix | Out-Null
& "$sdkBin\signtool.exe" sign /q /fd SHA256 /f $pfx /p $pfxPassword $msix
Copy-Item $vclibs "$out\Dependencies\"

"package: $msix"
"         $((Get-Item $msix).Length / 1MB -as [int]) MB, + Dependencies\Microsoft.VCLibs.x64.14.00.appx"
