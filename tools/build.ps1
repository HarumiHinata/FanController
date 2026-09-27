param([ValidateSet('Release','RelWithDebInfo')][string]$Configuration='Release',[switch]$Asan)
$ErrorActionPreference='Stop'
$vswhere='C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $installation) { throw 'Visual Studio 2022 C++ tools are required.' }
$cmake=Join-Path $installation 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
if(-not (Test-Path -LiteralPath $cmake)) { $cmake=(Get-Command cmake -ErrorAction Stop).Source }
Push-Location (Join-Path $PSScriptRoot '..')
try {
    $preset=if($Asan){'asan'}else{'windows'}
    & $cmake --preset $preset
    if($LASTEXITCODE -ne 0){throw 'Configure failed.'}
    $buildPreset=if($Asan){'asan'}else{'release'}
    & $cmake --build --preset $buildPreset
    if($LASTEXITCODE -ne 0){throw 'Build failed.'}
    if(-not $Asan) {
        $ctest=Join-Path (Split-Path $cmake) 'ctest.exe'
        & $ctest --preset release
        if($LASTEXITCODE -ne 0){throw 'Tests failed.'}
    }
} finally { Pop-Location }
