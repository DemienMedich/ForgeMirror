# ForgeMirror 0.6.28 Qt installer verification

- Canonical version: root `VERSION` = `0.6.28`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.28.exe`.
- Size: `11,524,369` bytes.
- SHA-256: `7DBD43D8B165299AFAE700CF3294D80FEC21BBE08EE77A948439B4E2C1C35353`.
- Setup ProductVersion and packaged EXE ProductVersion/FileVersion: `0.6.28`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; current-user installation (`PrivilegesRequired=lowest`).
- Payload: 28 files.

## Verification

- `build-qt.ps1 -Package`: passed; `smoke_qt` 1/1 and `smoke_core: OK`.
- `smoke_qt` verifies analog and digital clock visibility, local-time formatting, one-second timer, accessible names, and rendered clock face.
- Isolated lifecycle directory: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage182-final-668788f85efc40ed932f4da9d0c66084`. Installed 0.6.27, updated in-place to 0.6.28 under the stable AppId, and verified EXE ProductVersion/FileVersion plus HKCU uninstall DisplayVersion.
- Installed 0.6.28 launched with `PATH=C:\Windows\System32;C:\Windows`, Qt plugin environment overrides cleared, disposable `--storage-dir`, `--screenshot` and `--smoke-test`; exit code 0, screenshot 53,427 bytes.
- Silent uninstall exited 0 and removed the application directory. A separate user-data marker remained intact.
- Inno Setup 6.7.3 compiled successfully. It emitted the existing non-blocking `[UninstallRun]` warning about `RunOnceId`.

## Scope

Stage 182 restores the stable navigation sidebar's local analog and digital clock in Qt's navigation column. It uses the system's local time, shares the application palette, updates once per second, and adds accessible names. The user workspace and `develop` branch were not modified.
