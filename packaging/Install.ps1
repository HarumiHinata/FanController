param([switch]$DesktopShortcut)
$ErrorActionPreference='Stop'
$source=Join-Path $PSScriptRoot 'FanController.exe'
if(-not (Test-Path -LiteralPath $source)) { throw 'FanController.exe is missing beside Install.ps1.' }
$destination=Join-Path $env:LOCALAPPDATA 'Programs/FanController'
$running=@(Get-Process -Name FanController -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq (Join-Path $destination 'FanController.exe')})
if($running.Count) { throw 'Please choose Restore automatic and exit in FanController before installing.' }
New-Item -ItemType Directory -Force -Path $destination | Out-Null
foreach($name in @('FanController.exe','README.md','Lenovo-Fan-Control.txt','Uninstall.ps1','verification-report.md')) {
    $item=Join-Path $PSScriptRoot $name
    if(Test-Path -LiteralPath $item) { Copy-Item -LiteralPath $item -Destination (Join-Path $destination $name) -Force }
}
$shell=New-Object -ComObject WScript.Shell
try {
    $link=$shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Programs')) 'FanController.lnk'))
    $link.TargetPath=Join-Path $destination 'FanController.exe'; $link.WorkingDirectory=$destination; $link.Save()
    if($DesktopShortcut) {
        $link=$shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Desktop')) 'FanController.lnk'))
        $link.TargetPath=Join-Path $destination 'FanController.exe'; $link.WorkingDirectory=$destination; $link.Save()
    }
} finally { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($shell) }
$uninstallKey='HKCU:/Software/Microsoft/Windows/CurrentVersion/Uninstall/FanController'
New-Item -Path $uninstallKey -Force | Out-Null
New-ItemProperty -Path $uninstallKey -Name DisplayName -Value 'FanController' -PropertyType String -Force | Out-Null
New-ItemProperty -Path $uninstallKey -Name DisplayVersion -Value '0.1.0' -PropertyType String -Force | Out-Null
New-ItemProperty -Path $uninstallKey -Name InstallLocation -Value $destination -PropertyType String -Force | Out-Null
$uninstallCommand='powershell.exe -NoProfile -File "'+(Join-Path $destination 'Uninstall.ps1')+'"'
New-ItemProperty -Path $uninstallKey -Name UninstallString -Value $uninstallCommand -PropertyType String -Force | Out-Null
Write-Output "Installed: $destination"
Write-Output 'Autostart remains off. Enable it in the application if desired.'
