# Lists every import of a binary that a UWP app on the Xbox does not have.
# Adapted from KytyPS5's src/uwp/uwp.ps1 Test-Imports (MIT, Copyright KytyPS5 contributors).
#
# An import the Xbox cannot resolve stops the app from starting there, while Windows still runs it,
# so this is checked on the PC. Allowed: what WindowsApp.lib exports, the store C runtime
# (api-ms-win-crt-*, vcruntime140*_app, msvcp140*_app) and DLLs shipped in the package.
# Imports by ordinal are not checked.
#
#   tools\uwp-import-check.ps1 -Binary <exe or dll> [-Packaged a.dll,b.dll] [-Detail]
param(
    [Parameter(Mandatory)] [string] $Binary,
    [string[]] $Packaged = @(),
    [switch] $Detail
)
$ErrorActionPreference = 'Stop'
$llvm = 'D:\LLVM\clang+llvm-22.1.8-x86_64-pc-windows-msvc\bin'
$sdk = 'D:\Windows Kits\10\Lib\10.0.22621.0\um\x64\WindowsApp.lib'

$available = [System.Collections.Generic.HashSet[string]]::new()
foreach ($line in (& "$llvm\llvm-nm.exe" $sdk 2>$null)) {
    if ($line -match '__imp_(\S+)$') { [void]$available.Add($Matches[1]) }
}
if ($available.Count -eq 0) { throw "no exports read from $sdk" }

$dll = ''
$missing = foreach ($line in (& "$llvm\llvm-readobj.exe" --coff-imports $Binary)) {
    if ($line -match '^\s*Name: (\S+)') { $dll = $Matches[1]; continue }
    if ($line -notmatch '^\s*Symbol: ([^(\s]\S*)') { continue }
    $symbol = $Matches[1]
    if ($dll -match '^(api-ms-win-crt-|vcruntime140(_1)?_app|msvcp140.*_app)' -or
        $Packaged -contains $dll -or $available.Contains($symbol)) { continue }
    [pscustomobject]@{ Dll = $dll; Symbol = $symbol }
}

if (-not $missing) { "OK: every import of $(Split-Path $Binary -Leaf) is available to UWP apps"; return }
"{0} imports UWP apps on the Xbox do not have, in {1} DLLs:" -f @($missing).Count, @($missing | Select-Object -Unique Dll).Count
$missing | Group-Object Dll | Sort-Object Count -Descending | ForEach-Object {
    if ($Detail) { "  {0} ({1}): {2}" -f $_.Name, $_.Count, (($_.Group.Symbol | Sort-Object) -join ' ') }
    else { "  {0,-40} {1}" -f $_.Name, $_.Count }
}
