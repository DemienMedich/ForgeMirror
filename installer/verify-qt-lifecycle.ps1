param(
    [Parameter(Mandatory = $true)][string]$PreviousVersion,
    [Parameter(Mandatory = $true)][string]$CurrentVersion,
    [string]$PreviousPackageDirectory = "package-qt-$PreviousVersion-release",
    [string]$CurrentPackageDirectory = "package-qt-$CurrentVersion-release",
    [string]$IsccPath = ''
)

$ErrorActionPreference = 'Stop'
foreach ($version in @($PreviousVersion, $CurrentVersion)) {
    if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "Expected SemVer version, got '$version'." }
}
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$previousPackage = (Resolve-Path (Join-Path $repoRoot $PreviousPackageDirectory)).Path
$currentPackage = (Resolve-Path (Join-Path $repoRoot $CurrentPackageDirectory)).Path
if (-not $IsccPath) {
    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) { $IsccPath = $command.Source }
    foreach ($candidate in @('Z:\Soft\Inno Setup 6\ISCC.exe', 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe', "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe")) {
        if (-not $IsccPath -and (Test-Path -LiteralPath $candidate)) { $IsccPath = $candidate }
    }
}
if (-not $IsccPath -or -not (Test-Path -LiteralPath $IsccPath)) { throw 'ISCC.exe not found.' }

$token = [guid]::NewGuid().ToString('N').ToUpperInvariant()
$testDisplayName = "ForgeMirror lifecycle test $token"
$evidenceRoot = Join-Path $repoRoot "build-qt\lifecycle-$CurrentVersion-$token"
$installDirectory = Join-Path $env:LOCALAPPDATA "Programs\ForgeMirrorQtLifecycle-$token"
$workspace = Join-Path $evidenceRoot 'external-workspace'
$testScript = Join-Path $PSScriptRoot "ForgeMirrorQt.lifecycle-$token.iss"
$uninstallRoot = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall'
New-Item -ItemType Directory -Force -Path $evidenceRoot, $workspace | Out-Null
$markerPath = Join-Path $workspace 'user-data-marker.bin'
[IO.File]::WriteAllBytes($markerPath, [byte[]](0, 1, 2, 127, 128, 254, 255))
$markerHash = (Get-FileHash -LiteralPath $markerPath -Algorithm SHA256).Hash

$iss = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'ForgeMirrorQt.iss') -Raw -Encoding UTF8
$iss = $iss.Replace('AppId={{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}', "AppId={{$token}")
$iss = $iss.Replace('AppName=ForgeMirror', "AppName=$testDisplayName")
$iss = $iss.Replace('AppVerName=ForgeMirror {#AppVersion}', "AppVerName=$testDisplayName {#AppVersion}")
$iss = $iss.Replace('DefaultDirName={localappdata}\Programs\ForgeMirror', "DefaultDirName={localappdata}\Programs\ForgeMirrorQtLifecycle-$token")
$iss = $iss.Replace('Name: "{autoprograms}\ForgeMirror";', "Name: `"{autoprograms}\$testDisplayName`";")
$iss = $iss.Replace('Name: "{autodesktop}\ForgeMirror";', "Name: `"{autodesktop}\$testDisplayName`";")
[IO.File]::WriteAllText($testScript, $iss, [Text.UTF8Encoding]::new($false))

$savedEnvironment = @{
    PATH = $env:PATH
    QT_PLUGIN_PATH = $env:QT_PLUGIN_PATH
    QT_QPA_PLATFORM_PLUGIN_PATH = $env:QT_QPA_PLATFORM_PLUGIN_PATH
}
$results = [Collections.Generic.List[object]]::new()
try {
    foreach ($version in @($PreviousVersion, $CurrentVersion)) {
        $package = if ($version -eq $PreviousVersion) { $previousPackage } else { $currentPackage }
        $output = Join-Path $evidenceRoot "payload-$version"
        New-Item -ItemType Directory -Force -Path $output | Out-Null
        & $IsccPath "/DAppVersion=$version" "/DOutputDir=$output" "/DPackageRoot=$package" $testScript | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Test-only installer build failed for $version." }
        $setup = Join-Path $output "ForgeMirrorSetup_$version.exe"
        if (-not (Test-Path -LiteralPath $setup)) { throw "Test installer is missing: $setup" }
        $setupInfo = (Get-Item -LiteralPath $setup).VersionInfo
        if ($setupInfo.FileVersion.Trim() -ne $version -or $setupInfo.ProductVersion.Trim() -ne $version) {
            throw "Installer metadata does not match $version."
        }

        $setupLog = Join-Path $evidenceRoot "install-$version.log"
        $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', "/DIR=`"$installDirectory`"", "/LOG=`"$setupLog`"")
        $process = Start-Process -FilePath $setup -ArgumentList $arguments -Wait -PassThru
        if ($process.ExitCode -ne 0) { throw "Install/update to $version failed with exit code $($process.ExitCode)." }

        $installedExe = Join-Path $installDirectory 'ForgeMirrorQt.exe'
        if (-not (Test-Path -LiteralPath $installedExe)) { throw "Installed executable is missing for $version." }
        $installedVersion = (Get-Item -LiteralPath $installedExe).VersionInfo.ProductVersion.Trim()
        if ($installedVersion -ne $version) { throw "Installed EXE reports $installedVersion, expected $version." }
        $registryEntry = Get-ChildItem $uninstallRoot | ForEach-Object { Get-ItemProperty $_.PSPath } |
            Where-Object { $_.InstallLocation -and ([IO.Path]::GetFullPath($_.InstallLocation).TrimEnd('\') -ieq $installDirectory.TrimEnd('\')) } |
            Select-Object -First 1
        if (-not $registryEntry -or $registryEntry.DisplayVersion -ne $version) {
            throw "Uninstall registration does not match $version."
        }

        $env:PATH = 'C:\Windows\System32;C:\Windows'
        Remove-Item Env:QT_PLUGIN_PATH -ErrorAction SilentlyContinue
        Remove-Item Env:QT_QPA_PLATFORM_PLUGIN_PATH -ErrorAction SilentlyContinue
        $versionOutput = ''
        if ($version -eq $CurrentVersion) {
            $versionStdout = Join-Path $evidenceRoot "version-$version.stdout.txt"
            $versionStderr = Join-Path $evidenceRoot "version-$version.stderr.txt"
            $versionProcess = Start-Process -FilePath $installedExe -ArgumentList @('--version') -Wait -PassThru `
                -RedirectStandardOutput $versionStdout -RedirectStandardError $versionStderr
            if ($versionProcess.ExitCode -ne 0) { throw "Installed --version failed with exit $($versionProcess.ExitCode)." }
            $versionOutput = (Get-Content -LiteralPath $versionStdout -Raw -ErrorAction SilentlyContinue).Trim()
            if ($versionOutput -ne "ForgeMirrorQt $version") { throw "Installed --version output '$versionOutput' does not match $version." }
        }
        $screenshot = Join-Path $evidenceRoot "installed-$version.png"
        $smokeArguments = @('--smoke-test', '--screenshot', "`"$screenshot`"", '--storage-dir', "`"$workspace`"")
        $smokeProcess = Start-Process -FilePath $installedExe -ArgumentList $smokeArguments -PassThru
        $windowTitle = ''
        $windowDeadline = (Get-Date).AddSeconds(15)
        do {
            $smokeProcess.Refresh()
            if ($smokeProcess.HasExited) { break }
            $windowTitle = $smokeProcess.MainWindowTitle
            if (-not $windowTitle) { Start-Sleep -Milliseconds 100 }
        } while (-not $windowTitle -and (Get-Date) -lt $windowDeadline)
        if (-not $windowTitle.Contains($version)) {
            if (-not $smokeProcess.HasExited) { Stop-Process -Id $smokeProcess.Id -Force }
            throw "Installed window title '$windowTitle' does not contain version $version."
        }
        if (-not $smokeProcess.WaitForExit(15000)) {
            Stop-Process -Id $smokeProcess.Id -Force
            throw "Installed real-window smoke did not exit for $version."
        }
        if ($smokeProcess.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $screenshot)) {
            throw "Installed real-window smoke failed for $version (exit $($smokeProcess.ExitCode), screenshot $([bool](Test-Path -LiteralPath $screenshot)))."
        }

        $results.Add([pscustomobject]@{
            Version = $version
            Installer = $setup
            InstallerBytes = (Get-Item -LiteralPath $setup).Length
            InstallerSha256 = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash
            InstalledProductVersion = $installedVersion
            UninstallDisplayVersion = $registryEntry.DisplayVersion
            VersionCommandOutput = $versionOutput
            MainWindowTitle = $windowTitle
            Screenshot = $screenshot
            ScreenshotBytes = (Get-Item -LiteralPath $screenshot).Length
            ScreenshotSha256 = (Get-FileHash -LiteralPath $screenshot -Algorithm SHA256).Hash
        })
    }

    $uninstaller = Join-Path $installDirectory 'unins000.exe'
    if (-not (Test-Path -LiteralPath $uninstaller)) { throw 'Isolated uninstall program is missing.' }
    $uninstallLog = Join-Path $evidenceRoot 'uninstall.log'
    $removed = Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', "/LOG=`"$uninstallLog`"") -Wait -PassThru
    if ($removed.ExitCode -ne 0) { throw "Isolated uninstall failed with exit code $($removed.ExitCode)." }
    Start-Sleep -Milliseconds 500
    $remaining = Get-ChildItem $uninstallRoot | ForEach-Object { Get-ItemProperty $_.PSPath } |
        Where-Object { $_.InstallLocation -and ([IO.Path]::GetFullPath($_.InstallLocation).TrimEnd('\') -ieq $installDirectory.TrimEnd('\')) }
    if (Test-Path -LiteralPath $installDirectory) { throw 'Application directory remained after uninstall.' }
    if ($remaining) { throw 'Uninstall registry record remained after uninstall.' }
    $markerAfter = (Get-FileHash -LiteralPath $markerPath -Algorithm SHA256).Hash
    if ($markerAfter -ne $markerHash) { throw 'External user-data marker changed during uninstall.' }

    $results
    [pscustomobject]@{
        UninstallExitCode = $removed.ExitCode
        InstallDirectoryRemoved = $true
        UninstallRegistryRemoved = $true
        ExternalWorkspaceMarkerSha256 = $markerAfter
        EvidenceDirectory = $evidenceRoot
    }
} finally {
    $cleanupUninstaller = Join-Path $installDirectory 'unins000.exe'
    if (Test-Path -LiteralPath $cleanupUninstaller) {
        try {
            $cleanup = Start-Process -FilePath $cleanupUninstaller -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-') -Wait -PassThru
            if ($cleanup.ExitCode -ne 0) { Write-Warning "Lifecycle test cleanup exited with code $($cleanup.ExitCode)." }
        } catch { Write-Warning "Lifecycle test cleanup could not run: $_" }
    }
    if (Test-Path -LiteralPath $installDirectory) {
        $resolvedInstall = (Resolve-Path -LiteralPath $installDirectory).Path
        $expectedParent = (Resolve-Path -LiteralPath (Join-Path $env:LOCALAPPDATA 'Programs')).Path.TrimEnd('\') + '\'
        if (-not $resolvedInstall.StartsWith($expectedParent, [StringComparison]::OrdinalIgnoreCase) -or
            [IO.Path]::GetFileName($resolvedInstall) -ne "ForgeMirrorQtLifecycle-$token") {
            Write-Warning "Refusing to clean unexpected lifecycle path: $resolvedInstall"
        } else {
            [IO.Directory]::Delete($resolvedInstall, $true)
        }
    }
    $desktopPath = [Environment]::GetFolderPath([Environment+SpecialFolder]::DesktopDirectory)
    foreach ($shortcut in @(
        (Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\$testDisplayName.lnk"),
        (Join-Path $desktopPath "$testDisplayName.lnk")
    )) {
        if ([IO.File]::Exists($shortcut)) { [IO.File]::Delete($shortcut) }
    }
    $env:PATH = $savedEnvironment.PATH
    if ($null -ne $savedEnvironment.QT_PLUGIN_PATH) { $env:QT_PLUGIN_PATH = $savedEnvironment.QT_PLUGIN_PATH } else { Remove-Item Env:QT_PLUGIN_PATH -ErrorAction SilentlyContinue }
    if ($null -ne $savedEnvironment.QT_QPA_PLATFORM_PLUGIN_PATH) { $env:QT_QPA_PLATFORM_PLUGIN_PATH = $savedEnvironment.QT_QPA_PLATFORM_PLUGIN_PATH } else { Remove-Item Env:QT_QPA_PLATFORM_PLUGIN_PATH -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $testScript) { Remove-Item -LiteralPath $testScript -Force }
}
