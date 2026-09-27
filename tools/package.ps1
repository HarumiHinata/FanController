$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$destination=Join-Path $root 'dist/FanController-0.1.0-win64'
New-Item -ItemType Directory -Path $destination -Force | Out-Null
$files=@{
    'build/Release/FanController.exe'='FanController.exe'
    'README.md'='README.md'
    'third_party/notices/Lenovo-Fan-Control.txt'='Lenovo-Fan-Control.txt'
    'packaging/Install.ps1'='Install.ps1'
    'packaging/Uninstall.ps1'='Uninstall.ps1'
    'docs/verification-report.md'='verification-report.md'
}
foreach($entry in $files.GetEnumerator()) { Copy-Item -LiteralPath (Join-Path $root $entry.Key) -Destination (Join-Path $destination $entry.Value) -Force }
$hash=Get-FileHash -LiteralPath (Join-Path $destination 'FanController.exe') -Algorithm SHA256
"$($hash.Hash)  FanController.exe" | Set-Content -LiteralPath (Join-Path $destination 'SHA256SUMS.txt') -Encoding ascii
Compress-Archive -LiteralPath $destination -DestinationPath (Join-Path $root 'dist/FanController-0.1.0-win64.zip') -Force
Get-Item -LiteralPath (Join-Path $destination 'FanController.exe'),(Join-Path $root 'dist/FanController-0.1.0-win64.zip') | Select-Object FullName,Length
