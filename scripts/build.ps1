param(
    [string]$QtRoot = 'D:\Qt\6.8.3\msvc2022_64',
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmakePath = $cmakeCommand.Source }
else {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio C++ tools and CMake first.' }
    $vsPath = & $vswhere -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmakePath = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
if (!(Test-Path -LiteralPath (Join-Path $QtRoot 'lib\cmake\Qt6\Qt6Config.cmake'))) { throw "Qt SDK not found: $QtRoot" }
$buildPath = Join-Path $projectRoot 'build'
& $cmakePath -S $projectRoot -B $buildPath -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$QtRoot" -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& $cmakePath --build $buildPath --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
$env:PATH = (Join-Path $QtRoot 'bin') + ';' + $env:PATH
& (Join-Path $buildPath "$Configuration\fastsend_tests.exe") -o (Join-Path $buildPath 'test-results.txt,txt')
if ($LASTEXITCODE -ne 0) {
    Get-Content -LiteralPath (Join-Path $buildPath 'test-results.txt') -Encoding UTF8
    throw 'Tests failed.'
}
Get-Content -LiteralPath (Join-Path $buildPath 'test-results.txt') -Encoding UTF8
Write-Output "Built: $buildPath\$Configuration\FastSend.exe"
