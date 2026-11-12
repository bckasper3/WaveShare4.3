# Compile the whole ESP-IDF project, CONTINUING past errors, and show only errors/warnings in
# the folders you own. Used while several people rewrite different parts of main/ in parallel:
# errors elsewhere are expected and hidden.
# Usage:  powershell -File tools\idf_check.ps1 main/board main/rtc
#         powershell -File tools\idf_check.ps1            (everything)
$ErrorActionPreference = 'Continue'
$root  = Split-Path -Parent $PSScriptRoot
$idf   = if ($env:ENVLOGGER_IDF_PATH) { $env:ENVLOGGER_IDF_PATH } else { 'C:\esp\v5.5.6\esp-idf' }
$sha   = New-Object System.Security.Cryptography.SHA1Managed
$tag   = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($root.ToLowerInvariant())))).Replace('-', '').Substring(0, 8)
$build = Join-Path $env:LOCALAPPDATA "envlogger\build-idf-$tag"

. "$idf\export.ps1" | Out-Null
if (-not (Test-Path "$build\build.ninja")) {
    idf.py -C $root -B $build -D "SDKCONFIG=$build\sdkconfig" set-target esp32s3 | Out-Null
}
idf.py -C $root -B $build -D "SDKCONFIG=$build\sdkconfig" reconfigure | Out-Null
$log = ninja -C $build -k 0 -j 4 2>&1 | ForEach-Object { "$_" }
$pat = if ($args.Count) { ($args | ForEach-Object { [regex]::Escape(($_ -replace '\\', '/')) }) -join '|' } else { '.' }
$hits = $log | Where-Object { ($_ -match 'error:|warning:') -and ($_ -replace '\\', '/') -match $pat }
$hits | ForEach-Object { $_ -replace '^.*?/envlogger/', '' }
"--- $(@($hits | Where-Object { $_ -match 'error:' }).Count) errors in selected folders; " +
"$(@($log | Where-Object { $_ -match 'error:' }).Count) errors total"
