param(
    [string]$IsccPath = ""
)
$ErrorActionPreference = "Stop"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$version = (Get-Content -LiteralPath (Join-Path $repoRoot "VERSION") -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "VERSION must be SemVer MAJOR.MINOR.PATCH: $version" }
$exe = Join-Path $repoRoot "package-qt\ForgeMirrorQt.exe"
if (-not (Test-Path -LiteralPath $exe)) { throw "Missing Qt package: $exe. Run build-qt.ps1 -Package first." }
$productVersion = (Get-Item -LiteralPath $exe).VersionInfo.ProductVersion
if ($productVersion -ne $version) { throw "EXE version $productVersion does not match VERSION $version." }
if (-not $IsccPath) {
    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) { $IsccPath = $command.Source }
    foreach ($candidate in @('Z:\Soft\Inno Setup 6\ISCC.exe', 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe', "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe")) {
        if (-not $IsccPath -and (Test-Path -LiteralPath $candidate)) { $IsccPath = $candidate }
    }
}
if (-not $IsccPath -or -not (Test-Path -LiteralPath $IsccPath)) { throw "ISCC.exe not found." }
$output = Join-Path $repoRoot "dist"
New-Item -ItemType Directory -Force -Path $output | Out-Null
& $IsccPath "/DAppVersion=$version" "/DOutputDir=$output" (Join-Path $PSScriptRoot "ForgeMirrorQt.iss")
if ($LASTEXITCODE) { throw "Inno Setup build failed." }
$installer = Join-Path $output "ForgeMirrorSetup_$version.exe"
if (-not (Test-Path -LiteralPath $installer)) { throw "Installer not produced: $installer" }
Write-Host "Qt installer: $installer"
