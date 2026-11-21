param([string]$QtRoot = 'D:\Qt\6.8.3\msvc2022_64')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $projectRoot 'build\Release\FastSend.exe'
if (!(Test-Path -LiteralPath $executable)) { throw 'Run scripts\build.ps1 first.' }
$packagePath = Join-Path $projectRoot 'dist\FastSend-V-1-Windows-x64'
New-Item -ItemType Directory -Path $packagePath -Force | Out-Null
Copy-Item -LiteralPath $executable -Destination $packagePath -Force
$env:PATH = (Join-Path $QtRoot 'bin') + ';' + $env:PATH
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$env:VCINSTALLDIR = Join-Path $vsPath 'VC'
& (Join-Path $QtRoot 'bin\windeployqt.exe') --release --compiler-runtime --no-translations (Join-Path $packagePath 'FastSend.exe')
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
$redistRoot = Join-Path $env:VCINSTALLDIR 'Redist\MSVC'
$redistVersion = Get-ChildItem -LiteralPath $redistRoot -Directory | Where-Object { $_.Name -match '^14\.' } | Sort-Object Name -Descending | Select-Object -First 1
if (!$redistVersion) { throw 'Visual C++ redistributable files not found.' }
$crtDirectory = Join-Path $redistVersion.FullName 'x64\Microsoft.VC143.CRT'
Copy-Item -Path (Join-Path $crtDirectory '*.dll') -Destination $packagePath -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination $packagePath -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination $packagePath -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md') -Destination $packagePath -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'licenses') -Destination $packagePath -Recurse -Force
$zipPath = Join-Path $projectRoot 'dist\FastSend-V-1-Windows-x64.zip'
Compress-Archive -Path $packagePath -DestinationPath $zipPath -Force
Write-Output $zipPath
