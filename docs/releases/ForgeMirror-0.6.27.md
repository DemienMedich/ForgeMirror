# ForgeMirror 0.6.27 Qt installer verification

- Canonical version: root `VERSION` = `0.6.27`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.27.exe`.
- Size: `11,522,590` bytes.
- SHA-256: `33D2DA891967A662EC2515294407DCA20B91B0BD9F00CC1EEB9ADB9608D54F3D`.
- Setup ProductVersion and packaged EXE ProductVersion/FileVersion: `0.6.27`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; current-user installation (`PrivilegesRequired=lowest`).
- Payload: 28 files.

## Verification

- `build-qt.ps1 -Package`: passed; `smoke_qt` 1/1 and `smoke_core: OK`.
- `smoke_qt` covers global timer status updates, start/pause, manual next interval, reset, navigation to the timer page and module-disable visibility. A skipped active focus does not call the reward handler.
- Isolated lifecycle directory: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage181-final-187fd1ff315d4c06a1229ffa539035c3`. Installed 0.6.26, updated in-place to 0.6.27 under the stable AppId, and verified EXE ProductVersion/FileVersion plus HKCU uninstall DisplayVersion.
- Installed 0.6.27 launched with `PATH=C:\Windows\System32;C:\Windows`, Qt plugin environment overrides cleared, disposable `--storage-dir`, `--screenshot` and `--smoke-test`; exit code 0, screenshot 47,011 bytes.
- Silent uninstall exited 0 and removed the application directory. A separate user-data marker remained intact.
- Inno Setup 6.7.3 compiled successfully. It emitted the existing non-blocking `[UninstallRun]` warning about `RunOnceId`.

## Scope

Stage 181 brings the legacy navigation sidebar's quick Pomodoro state/actions into the Qt header and links them to the existing timer instance. Advancing before a focus completes does not award focus coins. The application palette and `develop` branch remain unchanged.
