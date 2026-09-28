# ForgeMirror 0.6.29 Qt installer verification

- Canonical version: root `VERSION` = `0.6.29`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.29.exe`.
- Size: `11,527,234` bytes.
- SHA-256: `0DDB4673F80A2333DE549C13814C0C1CA46DB77C57072CCB80AF4C5EB901A4F2`.
- Setup ProductVersion and packaged EXE ProductVersion/FileVersion: `0.6.29`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; current-user installation (`PrivilegesRequired=lowest`).
- Payload: 28 files.

## Verification

- `build-qt.ps1 -Package`: passed; `smoke_qt` 1/1 and `smoke_core: OK`.
- Quick-sync service tests verify viewer pull and administrator push obey their configured direction while periodic auto-sync is disabled; the periodic runner remains a no-op. UI tests cover header visibility, status icon, menu navigation and disabled-cloud feedback.
- Packaged `ForgeMirrorQt.exe` started with `PATH=C:\Windows\System32;C:\Windows`, Qt plugin overrides cleared, disposable `--storage-dir`, `--screenshot` and `--smoke-test`; exit code 0, screenshot 52,527 bytes.
- Isolated lifecycle directory: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage183-final-46658a7a7eba469998f7c20862691ae7`. Installed 0.6.28 and updated in place to 0.6.29 under the stable AppId. EXE ProductVersion/FileVersion and HKCU uninstall DisplayVersion matched 0.6.29.
- Installed 0.6.29 launched with Qt removed from `PATH` and plugin environment overrides cleared, using a disposable workspace; exit code 0, screenshot 54,466 bytes.
- Silent uninstall exited 0 and removed the application directory. A separate user-data marker remained intact.
- Inno Setup 6.7.3 compiled successfully. It emitted the existing non-blocking `[UninstallRun]` warning about `RunOnceId`.

## Scope

Stage 183 adds a global Qt header action matching the stable client's configured-direction quick sync, including an unlocked viewer wallet upload, and a two-second cloud health/drift indicator. It uses Qt's journaled push and pull transactions and does not turn on periodic synchronization. The Qt palette is unchanged. The workspace and `develop` branch were not modified.
