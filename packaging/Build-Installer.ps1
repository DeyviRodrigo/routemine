param(
    [string]$Viewer,
    [string]$Environment,
    [string]$Destination,
    [string]$Nsis,
    [string]$ExampleProject,
    [string]$ExampleTerrain
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Viewer) { $Viewer = Join-Path $repo 'out\viewer\Release\terrain-options\RouteTec.exe' }
if (-not $Environment) { $Environment = Join-Path $repo 'out\viewer-deps' }
if (-not $Destination) { $Destination = Join-Path $repo 'entrega\RouteTec' }
if (-not $Nsis) { $Nsis = Join-Path $repo 'out\tooling\nsis\nsis-3.11\makensis.exe' }
foreach ($required in @($Viewer, $Nsis, (Join-Path $Environment 'python.exe'))) {
    if (-not (Test-Path -LiteralPath $required)) { throw "No se encuentra $required" }
}
$Destination = [IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$stageParent = Join-Path $repo ('out\package-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$stage = Join-Path $stageParent 'RouteTec'
$deployArgs = @((Join-Path $PSScriptRoot 'deploy_runtime.py'), '--viewer', $Viewer, '--environment', $Environment, '--output', $stage, '--repository', $repo)
if ($ExampleProject -or $ExampleTerrain) {
    if (-not $ExampleProject -or -not $ExampleTerrain) { throw 'Indique tanto ExampleProject como ExampleTerrain.' }
    $deployArgs += @('--example-project', $ExampleProject, '--example-terrain', $ExampleTerrain)
}
& (Join-Path $Environment 'python.exe') @deployArgs
if ($LASTEXITCODE -ne 0) { throw 'No se pudo preparar el runtime.' }
$installer = Join-Path $Destination 'RouteTec-Setup-1.0.0.exe'
$uninstall = Join-Path $stageParent 'uninstall-files.nsh'
& $Nsis /INPUTCHARSET UTF8 "/DSTAGE=$stage" "/DOUTPUT=$installer" "/DUNINSTALL_FILES=$uninstall" (Join-Path $PSScriptRoot 'RouteTec.nsi')
if ($LASTEXITCODE -ne 0) { throw 'No se pudo compilar el instalador.' }
$portable = Join-Path $Destination 'RouteTec-Portable-1.0.0.zip'
Compress-Archive -LiteralPath $stage -DestinationPath $portable -Force
$records = @($installer, $portable) | ForEach-Object {
    $file = Get-Item -LiteralPath $_
    [ordered]@{file=$file.Name;bytes=$file.Length;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLower()}
}
[ordered]@{product='RouteTec';version='1.0.0';build_time=(Get-Date -Format o);viewer_sha256=(Get-FileHash -LiteralPath $Viewer -Algorithm SHA256).Hash.ToLower();staging=$stage;files=$records} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Destination 'Distribucion.json') -Encoding utf8
Write-Output "Instalador: $installer"
Write-Output "Portable: $portable"
Write-Output "Staging: $stage"
