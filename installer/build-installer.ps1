param(
    [string]$Configuration = "Release",
    [string]$IssPath = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

$cmakePath = Join-Path $repoRoot "CMakeLists.txt"
$version = "0.0.0"
if (Test-Path $cmakePath) {
    # allow suffix after patch (e.g. 0.2.96-beta1)
    $match = Select-String -Path $cmakePath -Pattern 'APP_VERSION="([0-9]+\.[0-9]+\.[0-9]+[^"]*)"' | Select-Object -First 1
    if ($match) {
        $version = $match.Matches[0].Groups[1].Value
    }
}
if ($version -eq "0.0.0") {
    throw "APP_VERSION not found in CMakeLists.txt (found $version)."
}
Write-Host "Building installer with version $version"

$guiExe = Join-Path $repoRoot "build-gui\\$Configuration\\ForgeMirrorGui.exe"
$guiDll = Join-Path $repoRoot "build-gui\\$Configuration\\glfw3.dll"
$required = @($guiExe, $guiDll)
foreach ($path in $required) {
    if (-not (Test-Path $path)) {
        throw "Missing build output: $path. Build Release first."
    }
}

$iss = if ($IssPath -ne "") { $IssPath } else { Join-Path $PSScriptRoot "ForgeMirror.iss" }
if (-not (Test-Path $iss)) {
    throw "Installer script not found: $iss"
}

$isscc = $null
$cmd = Get-Command ISCC.exe -ErrorAction SilentlyContinue
if ($cmd) {
    $isscc = $cmd.Source
} else {
    $fallback = Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\\ISCC.exe"
    if (Test-Path $fallback) {
        $isscc = $fallback
    }
}

if (-not $isscc) {
    throw "ISCC.exe not found. Install Inno Setup 6 or add ISCC.exe to PATH."
}

$outputDir = Join-Path $repoRoot "dist"
& $isscc "/DAppVersion=$version" "/DOutputDir=$outputDir" $iss
