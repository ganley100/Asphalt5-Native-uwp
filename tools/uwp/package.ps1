<#
.SYNOPSIS
  Pack and sign the UWP / Xbox One build into an .appx.

.DESCRIPTION
  Takes the AppxLayout folder produced by the UWP CMake build
  (cmake -DCMAKE_TOOLCHAIN_FILE=cmake/uwp-x64.cmake) and runs makeappx and
  signtool from the Windows 10/11 SDK.  A self-signed certificate whose
  subject matches the manifest publisher is created on first use.

  Output in -Out:
    Asphalt5Native_x64.appx    the signed package
    Asphalt5Native.cer         its public certificate
    Dependencies\              Microsoft.VCLibs.140.00 (x64), when the SDK has it

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\uwp\package.ps1 -Layout build-uwp\AppxLayout
#>
param(
  [string]$Layout = "build-uwp\AppxLayout",
  [string]$Out = "dist",
  [string]$Publisher = "CN=Asphalt5Native",
  [string]$PfxPassword = "asphalt5-local"
)
$ErrorActionPreference = "Stop"

function Find-SdkTool([string]$name) {
  $kits = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
  $tool = Get-ChildItem $kits -Directory -Filter "10.*" -ErrorAction SilentlyContinue |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Join-Path $_.FullName "x64\$name" } |
    Where-Object { Test-Path $_ } |
    Select-Object -First 1
  if (-not $tool) { throw "$name not found. Install the Windows 10/11 SDK." }
  return $tool
}

if (-not (Test-Path (Join-Path $Layout "Asphalt5.exe"))) {
  throw "No Asphalt5.exe in $Layout. Build the UWP target first (see docs/XBOX.md)."
}
$makeappx = Find-SdkTool "makeappx.exe"
$signtool = Find-SdkTool "signtool.exe"
New-Item -ItemType Directory -Force $Out | Out-Null

$appx = Join-Path $Out "Asphalt5Native_x64.appx"
& $makeappx pack /o /h SHA256 /d $Layout /p $appx
if ($LASTEXITCODE) { throw "makeappx failed ($LASTEXITCODE)" }

# Throwaway signing key: Dev Mode sideloading only needs a signature whose
# subject matches the manifest Publisher.  The .pfx stays local.
$pfx = Join-Path $Out "Asphalt5Native.pfx"
$cer = Join-Path $Out "Asphalt5Native.cer"
$pw = ConvertTo-SecureString $PfxPassword -AsPlainText -Force
if (-not (Test-Path $pfx)) {
  $cert = New-SelfSignedCertificate -Type Custom -Subject $Publisher `
    -KeyUsage DigitalSignature -FriendlyName "Asphalt 5 Native UWP (self-signed)" `
    -CertStoreLocation "Cert:\CurrentUser\My" `
    -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")
  Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $pw | Out-Null
  Export-Certificate -Cert $cert -FilePath $cer | Out-Null
}
& $signtool sign /fd SHA256 /f $pfx /p $PfxPassword $appx
if ($LASTEXITCODE) { throw "signtool failed ($LASTEXITCODE)" }

# ANGLE needs the Visual C++ app runtime framework package.
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} "Microsoft SDKs\Windows Kits\10\ExtensionSDKs\Microsoft.VCLibs\14.0\Appx\Retail\x64"
$vclibs = Get-ChildItem (Join-Path $sdkRoot "*.appx") -ErrorAction SilentlyContinue | Select-Object -First 1
if ($vclibs) {
  $deps = Join-Path $Out "Dependencies"
  New-Item -ItemType Directory -Force $deps | Out-Null
  Copy-Item $vclibs.FullName $deps
} else {
  Write-Warning "Microsoft.VCLibs.x64.14.00.appx not found in the SDK; most consoles already have it."
}

Write-Host "Package:     $appx"
Write-Host "Certificate: $cer"
