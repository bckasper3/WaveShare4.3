# Run idf.py for this project with the build folder outside the synced drive.
# Usage:  powershell -File tools\idf.ps1 build
#         powershell -File tools\idf.ps1 -p COM5 flash monitor
# ESP-IDF location: $env:ENVLOGGER_IDF_PATH, default C:\esp\v5.5.6\esp-idf (tools\install notes in docs/DEVELOPMENT_PLAN.md).
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$idf  = if ($env:ENVLOGGER_IDF_PATH) { $env:ENVLOGGER_IDF_PATH } else { 'C:\esp\v5.5.6\esp-idf' }
if (-not (Test-Path "$idf\export.ps1")) { Write-Error "ESP-IDF not found at $idf (set ENVLOGGER_IDF_PATH)"; exit 2 }

$sha   = New-Object System.Security.Cryptography.SHA1Managed
$tag   = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($root.ToLowerInvariant())))).Replace('-', '').Substring(0, 8)
$build = Join-Path $env:LOCALAPPDATA "envlogger\build-idf-$tag"

. "$idf\export.ps1" | Out-Null
idf.py -C $root -B $build -D "SDKCONFIG=$build\sdkconfig" @args
exit $LASTEXITCODE
