# ForgeMirror 0.6.33 Qt installer verification

- Canonical version: root `VERSION` = `0.6.33`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.33.exe`.
- Size: `11,532,438` bytes.
- SHA-256: `9F444ECB3CF58D4979D1593384B8CD2BE7B9564C2C8BF66B1620C9B0669E4DEF`.
- Setup ProductVersion and packaged EXE ProductVersion/FileVersion: `0.6.33`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; current-user installation (`PrivilegesRequired=lowest`).
- Payload: 28 files.

## Verification

- `build-qt.ps1 -Package`: passed; `smoke_qt` 1/1 and `smoke_core: OK`.
- The administrator auth test submits the actual Qt login dialog with **Не выходить после перезапуска** checked, then launches three independent processes. Each restores administrator mode, retains the credential record and produces no rejected-login event.
- Telemetry tests save through the real Qt display-settings dialog and 3D-viewer settings page, then verify generic persisted events without paths or setting values. The display-settings test releases its read handle before later atomic log-file rewrites, which is required on Windows.
- Lifecycle directory: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage187-telemetry-20260928`. Installed 0.6.32 and updated in place to 0.6.33 using the stable AppId. EXE ProductVersion and HKCU uninstall DisplayVersion matched after each step.
- Installed 0.6.33 launched with Qt removed from `PATH` and Qt plugin overrides cleared, using a disposable workspace; exit code 0, screenshot 53,927 bytes.
- Silent uninstall exited 0, removed the test application directory and uninstall entry, and preserved a separate user-data marker.
- The pre-existing Qt process list was unchanged through lifecycle verification; the stable ImGui uninstall record remained unchanged.
- Inno Setup 6.7.3 compiled successfully. It emitted the existing non-blocking `[UninstallRun]` warning about `RunOnceId`.

## Scope

Stage 187 adds privacy-safe telemetry for successful Qt display and model-viewer settings saves. No selected paths, background names, setting values, production profile data or credentials were written or used by these changes. `develop` and the stable ImGui implementation remain unchanged.
