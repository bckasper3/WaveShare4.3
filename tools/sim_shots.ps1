# Build the PC UI simulator (sim/) and render every screen to PNG files.
# Usage:  powershell -File tools\sim_shots.ps1 [-Clean] [-Out <dir>] [-Run]
#   default output: %LOCALAPPDATA%\envlogger\shots   (outside the repo)
#   -Run  : after building, open the interactive simulator window instead of taking shots.
# LVGL and SDL2 are downloaded into the build folder on the first run (needs internet once).
param([switch]$Clean, [string]$Out = '', [switch]$Run)
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$src   = Join-Path $root 'sim'
$sha   = New-Object System.Security.Cryptography.SHA1Managed
$tag   = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($root.ToLowerInvariant())))).Replace('-', '').Substring(0, 8)
$build = Join-Path $env:LOCALAPPDATA "envlogger\build-sim-$tag"
if (-not $Out) { $Out = Join-Path $env:LOCALAPPDATA 'envlogger\shots' }

if ($Clean -and (Test-Path $build)) { Remove-Item -Recurse -Force $build }
cmake -S $src -B $build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release | Out-Host
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build $build | Out-Host
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$exe = Join-Path $build 'el_sim.exe'
if ($Run) { & $exe; exit $LASTEXITCODE }

& $exe --navtest
if ($LASTEXITCODE -ne 0) { 'Navigation test FAILED'; exit $LASTEXITCODE }

New-Item -ItemType Directory -Force $Out | Out-Null
Get-ChildItem $Out -Filter '*.png' -ErrorAction SilentlyContinue | Remove-Item -Force
& $exe --shots $Out
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$n = @(Get-ChildItem $Out -Filter '*.png').Count
"--- $n screenshots in $Out"
exit 0
