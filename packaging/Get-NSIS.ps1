param([string]$Destination)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Destination) { $Destination = Join-Path $repo 'out\tooling\nsis' }
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$archive = Join-Path $Destination 'nsis-3.11.zip'
$uri = 'https://github.com/tauri-apps/binary-releases/releases/download/nsis-3.11/nsis-3.11.zip'
# SHA-256 público del ZIP de NSIS; comprobado contra el archivo descargado.
$expected = 'c7d27f780ddb6cffb4730138cd1591e841f4b7edb155856901cdf5f214394fa1' # pragma: allowlist secret
if (-not (Test-Path -LiteralPath $archive)) { Invoke-WebRequest -Uri $uri -OutFile $archive }
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLower() -ne $expected) {
    throw 'El archivo NSIS no coincide con la versión fijada. No se ejecutó.'
}
Expand-Archive -LiteralPath $archive -DestinationPath $Destination -Force
$compiler = Join-Path $Destination 'nsis-3.11\makensis.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw 'La distribución no contiene makensis.exe.' }
Write-Output $compiler
