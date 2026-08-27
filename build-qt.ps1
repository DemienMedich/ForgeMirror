param(
    [string]$QtRoot = 'Z:\Soft\Qt\6.8.3\msvc2022_64',
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
& cmake -S $repo -B "$repo\build-qt" -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$QtRoot" -DBUILD_QT_GUI=ON -DBUILD_CORE_TESTS=ON -DBUILD_IMGUI_GUI=OFF
if ($LASTEXITCODE) { throw 'Qt configure failed' }
& cmake --build "$repo\build-qt" --config Release --target ForgeMirrorQt smoke_qt smoke_core
if ($LASTEXITCODE) { throw 'Qt build failed' }
$savedPath = $env:PATH
try {
    $env:PATH = "$QtRoot\bin;$savedPath"
    & ctest --test-dir "$repo\build-qt" -C Release --output-on-failure
    if ($LASTEXITCODE) { throw 'Qt tests failed' }
    & "$repo\build-qt\Release\smoke_core.exe"
    if ($LASTEXITCODE) { throw 'Core smoke tests failed' }
} finally { $env:PATH = $savedPath }
if ($Package) {
    $output = "$repo\package-qt"
    New-Item -ItemType Directory -Force -Path $output | Out-Null
    Copy-Item -LiteralPath "$repo\build-qt\Release\ForgeMirrorQt.exe" -Destination $output -Force
    & "$QtRoot\bin\windeployqt.exe" --release --no-translations --no-opengl-sw --no-system-d3d-compiler "$output\ForgeMirrorQt.exe"
    if ($LASTEXITCODE) { throw 'Qt deployment failed' }
    # App-local runtime for this host's custom MSVC installation (windeployqt may not discover it).
    $runtime = 'Z:\Soft\VC2022\VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT'
    if (Test-Path -LiteralPath $runtime) {
        Get-ChildItem -LiteralPath $runtime -Filter '*.dll' | Copy-Item -Destination $output -Force
    }
    Write-Host "Qt package: $output\ForgeMirrorQt.exe"
}
