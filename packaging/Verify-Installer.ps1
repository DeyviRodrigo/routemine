param(
    [Parameter(Mandatory=$true)][string]$Installer,
    [string]$Target,
    [string]$ReportDirectory
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
if (-not $Target) { $Target = Join-Path $repo "out\installed-qa-$stamp" }
if (-not $ReportDirectory) { $ReportDirectory = Join-Path $repo "out\installer-qa-$stamp" }
$Target = [IO.Path]::GetFullPath($Target)
$ReportDirectory = [IO.Path]::GetFullPath($ReportDirectory)
if (Test-Path -LiteralPath $Target) { throw 'La carpeta de prueba ya existe. Use una carpeta nueva.' }
if (Test-Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\RouteTec') {
    throw 'Ya existe una instalación de RouteTec para este usuario; no se modificará durante la prueba.'
}
New-Item -ItemType Directory -Path $ReportDirectory -Force | Out-Null
$process = Start-Process -FilePath ([IO.Path]::GetFullPath($Installer)) -ArgumentList @('/S', "/D=$Target") -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "Instalación fallida: $($process.ExitCode)" }
$manifest = Get-Content -LiteralPath (Join-Path $Target 'manifest.json') -Raw | ConvertFrom-Json
foreach ($item in $manifest.files) {
    $path = Join-Path $Target $item.path
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLower() -ne $item.sha256) {
        throw "No coincide el archivo instalado: $($item.path)"
    }
}
$originalPath = $env:PATH
$originalQt = $env:QT_PLUGIN_PATH
$originalQml = $env:QML2_IMPORT_PATH
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    Remove-Item Env:QT_PLUGIN_PATH -ErrorAction SilentlyContinue
    Remove-Item Env:QML2_IMPORT_PATH -ErrorAction SilentlyContinue
    $smoke = Join-Path $ReportDirectory 'smoke'
    New-Item -ItemType Directory -Path $smoke -Force | Out-Null
    $process = Start-Process -FilePath (Join-Path $Target 'RouteTec.exe') -ArgumentList @('--smoke-test', (Join-Path $Target 'Ejemplos\optimization-flat.obj'), $smoke) -WindowStyle Hidden -RedirectStandardOutput (Join-Path $smoke 'stdout.txt') -RedirectStandardError (Join-Path $smoke 'stderr.txt') -PassThru
    if (-not $process.WaitForExit(60000)) { $process.Kill(); throw 'La prueba de ejecución excedió 60 segundos.' }
    if ($process.ExitCode -ne 0 -or (Get-Content -LiteralPath (Join-Path $smoke 'stdout.txt') -Raw) -notmatch 'smoke=passed') {
        throw 'Falló la prueba funcional con PATH limpio. Consulte los registros de ejecución.'
    }
} finally {
    $env:PATH = $originalPath
    if ($null -eq $originalQt) { Remove-Item Env:QT_PLUGIN_PATH -ErrorAction SilentlyContinue } else { $env:QT_PLUGIN_PATH = $originalQt }
    if ($null -eq $originalQml) { Remove-Item Env:QML2_IMPORT_PATH -ErrorAction SilentlyContinue } else { $env:QML2_IMPORT_PATH = $originalQml }
}
$sentinel = Join-Path $Target 'Proyecto_personal_de_prueba.json'
$contents = '{"qa":"This file belongs to the user and must survive uninstall."}'
[IO.File]::WriteAllText($sentinel, $contents)
$process = Start-Process -FilePath (Join-Path $Target 'Desinstalar.exe') -ArgumentList '/S' -WindowStyle Hidden -Wait -PassThru
$deadline = (Get-Date).AddSeconds(30)
while ((Test-Path -LiteralPath (Join-Path $Target 'RouteTec.exe')) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 250 }
if (Test-Path -LiteralPath (Join-Path $Target 'RouteTec.exe')) { throw 'El ejecutable no fue desinstalado.' }
if ([IO.File]::ReadAllText($sentinel) -ne $contents) { throw 'No se conservó el proyecto del usuario.' }
if (Test-Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\RouteTec') { throw 'No se limpió el registro de desinstalación.' }
[ordered]@{
    installer=[IO.Path]::GetFullPath($Installer)
    installer_sha256=(Get-FileHash -LiteralPath $Installer -Algorithm SHA256).Hash.ToLower()
    target=$Target
    installed_files_verified=$manifest.files.Count
    smoke_clean_path='passed'
    uninstall='passed'
    user_project_preserved=$sentinel
    tested_at=(Get-Date -Format o)
} | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $ReportDirectory 'verification.json') -Encoding utf8
Write-Output "Verificación completa: $ReportDirectory\verification.json"
