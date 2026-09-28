# ForgeMirror Qt 0.6.46 installer verification

- Canonical version: root `VERSION` = `0.6.46`.
- Production installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.46.exe`.
- Installer size: 11,553,453 bytes; SHA-256: `781AAABDED3B1CD3B96BA1D19C8B8F0B9B623C854618E67E98D14314CDEDA5FA`.
- Installer ProductVersion and packaged EXE FileVersion/ProductVersion: `0.6.46`.
- Packaged EXE size: 3,338,240 bytes; SHA-256: `8AC93058CABD006AD778563F7C8F79F80DD644E5204566B7B7709B38A54D0925`.
- Inno Setup 6.7.3 compiled the production installer. The stable production AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; install is per-user. Payload: 28 files.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-next6` passed: `smoke_qt` 1/1 in 22.84 seconds and `smoke_core: OK`. `smoke_qt` includes immediate rollback preservation of externally changed file bytes, the preserved-copy manifest, restored pre-image and the path reported to the user.
- `git diff --check` passed before release staging.
- Isolated installer lifecycle used test-only AppId `{8B59E3F5-DEF5-4136-A108-E6EAF4ECC6B2}` under `Z:\CPP\ForgeMirror\build-qt\installer-lifecycle-0.6.46`. It installed 0.6.45 and updated in place to 0.6.46 under the same directory. At each step, EXE ProductVersion and HKCU uninstall `DisplayVersion` matched the installed version.
- The test-only installer script disabled application closing so it could not close the separate production 0.6.45 window; the production `.iss` keeps `CloseApplications=yes`. The test-only AppId, install directory, local/roaming app data and workspace were isolated.
- Installed 0.6.46 returned `ForgeMirrorQt 0.6.46` and passed real-window startup smoke with `PATH=C:\Windows\System32;C:\Windows`, Qt-related environment variables removed, and isolated `LOCALAPPDATA`, `APPDATA` and workspace. Exit code 0; stderr empty. Screenshot: `Z:\CPP\ForgeMirror\build-qt\installer-lifecycle-0.6.46\installed-smoke.png` (51,296 bytes).
- Silent uninstall exited 0, removed the test EXE and uninstall registry entry, and preserved a marker outside the install directory. The user's production 0.6.45 process remained alive with its original window title throughout the isolated cycle.
- Production installation and user workspace remain at 0.6.45 for now; they were not updated or written by this release lifecycle test.

## Scope

Stage 201 makes checked in-process rollback preserve changed, created or missing in-flight files before restoring journal pre-images. This protects data after a transaction failure; it does not coordinate external writers during an operation or close the stale-task race between comparison and replacement. Hands-on NVDA/JAWS interaction and broader non-UI core telemetry also remain open migration work. Functional parity remains an expert estimate of about 90%, not a measured code/test percentage. Stable ImGui and `develop` remain unchanged.
