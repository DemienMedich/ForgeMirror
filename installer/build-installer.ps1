param(
    [string]$Configuration = "Release",
    [string]$IssPath = "",
    [string]$IsccPath = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

$cmakePath = Join-Path $repoRoot "CMakeLists.txt"
$version = "0.0.0"
if (Test-Path $cmakePath) {
    # allow suffix after patch (e.g. 0.2.96-beta1)
    $match = Select-String -Path $cmakePath -Pattern 'set\(APP_VERSION\s+"([^"]+)"\)' | Select-Object -First 1
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
if ($IsccPath -ne "") {
    if (-not (Test-Path $IsccPath)) {
        throw "ISCC.exe override path not found: $IsccPath"
    }
    $isscc = $IsccPath
} else {
    $cmd = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($cmd) {
        $isscc = $cmd.Source
    } else {
        $candidateDirs = New-Object System.Collections.Generic.List[string]
        $registryRoots = @(
            "HKLM:\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\*",
            "HKLM:\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\*",
            "HKCU:\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\*",
            "HKCU:\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\*"
        )
        foreach ($root in $registryRoots) {
            Get-ItemProperty $root -ErrorAction SilentlyContinue |
                Where-Object { $_.DisplayName -like "*Inno Setup*" -and $_.InstallLocation } |
                ForEach-Object { $candidateDirs.Add($_.InstallLocation) }
        }
        $staticDirs = @(
            (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6"),
            (Join-Path ${env:ProgramFiles} "Inno Setup 6"),
            (Join-Path $env:LOCALAPPDATA "Programs\\Inno Setup 6")
        )
        foreach ($dir in $staticDirs) {
            if ($dir) { $candidateDirs.Add($dir) }
        }
        foreach ($dir in ($candidateDirs | Select-Object -Unique)) {
            $candidate = Join-Path $dir "ISCC.exe"
            if (Test-Path $candidate) {
                $isscc = $candidate
                break
            }
        }
    }
}

if (-not $isscc) {
    throw "ISCC.exe not found. Install Inno Setup 6 or add ISCC.exe to PATH (or pass -IsccPath)."
}

$outputDir = Join-Path $repoRoot "dist"
& $isscc "/DAppVersion=$version" "/DOutputDir=$outputDir" $iss
