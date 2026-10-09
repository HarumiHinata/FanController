$ErrorActionPreference='Stop'
$expected=[IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Programs/FanController')).TrimEnd('\')
$actual=[IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
if($actual -ne $expected) { throw "Run the installed uninstaller in $expected." }
$exe=Join-Path $actual 'FanController.exe'
if(@(Get-Process -Name FanController -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $exe}).Count) { throw 'Please choose Restore automatic and exit before uninstalling.' }
if(Test-Path -LiteralPath $exe) {
    $task=Start-Process -FilePath $exe -ArgumentList '--startup-off' -WindowStyle Hidden -Wait -PassThru
    if($task.ExitCode -ne 0) { throw 'Could not remove the autostart task. Uninstall stopped.' }
}
$shell=New-Object -ComObject WScript.Shell
try {
    foreach($folder in @([Environment]::GetFolderPath('Programs'),[Environment]::GetFolderPath('Desktop'))) {
        $linkPath=Join-Path $folder 'FanController.lnk'
        if(Test-Path -LiteralPath $linkPath) { $link=$shell.CreateShortcut($linkPath); if($link.TargetPath -eq $exe) { Remove-Item -LiteralPath $linkPath } }
    }
} finally { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($shell) }
foreach($name in @('FanController.exe','README.md','Lenovo-Fan-Control.txt','AMD-ADL.txt','verification-report.md','Uninstall.ps1')) {
    $item=[IO.Path]::GetFullPath((Join-Path $actual $name))
    if([IO.Path]::GetDirectoryName($item) -ne $expected) { throw 'Unexpected uninstall path.' }
    if(Test-Path -LiteralPath $item) { Remove-Item -LiteralPath $item }
}
if(-not (Get-ChildItem -LiteralPath $actual -Force | Select-Object -First 1)) { Remove-Item -LiteralPath $actual }
$key='HKCU:/Software/Microsoft/Windows/CurrentVersion/Uninstall/FanController'
if(Test-Path -LiteralPath $key) { Remove-Item -LiteralPath $key }
Write-Output 'FanController removed. Preferences and diagnostic logs have been retained.'
