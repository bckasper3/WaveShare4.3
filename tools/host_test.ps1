# Build and run the envlogger_core host unit tests (gcc + CMake + Ninja on the PC).
# Usage:  powershell -File tools\host_test.ps1 [-Clean]
# Exit code 0 = all tests passed.
param([switch]$Clean)
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$src   = Join-Path $root 'test\host'
# Build outside the synced drive (thousands of small files do not belong in a synced folder), and one
# build folder per checkout so parallel worktrees never share build state.
$sha   = New-Object System.Security.Cryptography.SHA1Managed
$tag   = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($root.ToLowerInvariant())))).Replace('-', '').Substring(0, 8)
$build = Join-Path $env:LOCALAPPDATA "envlogger\build-host-$tag"

if ($Clean -and (Test-Path $build)) { Remove-Item -Recurse -Force $build }
cmake -S $src -B $build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Debug | Out-Host
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build $build | Out-Host
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $build 'host_tests.exe')
exit $LASTEXITCODE
