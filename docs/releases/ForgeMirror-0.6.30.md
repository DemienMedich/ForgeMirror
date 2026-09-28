# ForgeMirror 0.6.30 Qt installer verification

- Canonical version: root `VERSION` = `0.6.30`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.30.exe`.
- Size: `11,527,142` bytes.
- SHA-256: `EA8376014F1B3326F61A92F97DC1E22FAC011A5A6DE5340D7D485AF8A070F39F`.
- Setup ProductVersion and packaged EXE ProductVersion/FileVersion: `0.6.30`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; current-user installation (`PrivilegesRequired=lowest`).
- Payload: 28 files.

## Verification

- `build-qt.ps1 -Package`: passed; `smoke_qt` 1/1 and `smoke_core: OK`.
- Administrator login end-to-end test now checks in through the real dialog with **Не выходить после перезапуска** enabled, then starts three distinct Qt client processes. Every launch restores the session, the test password stays unchanged and no rejected-login event appears.
- Lifecycle directory: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage184-auth-20260928`. Installed 0.6.29 and updated in place to 0.6.30 using the stable AppId. After each step the EXE ProductVersion and HKCU uninstall DisplayVersion matched; install location remained the isolated test directory.
- Installed 0.6.30 launched with Qt removed from `PATH` and plugin environment overrides cleared, using a disposable workspace; exit code 0, screenshot 53,617 bytes.
- Silent uninstall exited 0, removed the test application directory and uninstall entry, and preserved a separate user-data marker.
- The pre-existing Qt process list was unchanged through the lifecycle test; the existing stable ImGui uninstall record also remained unchanged.
- Inno Setup 6.7.3 compiled successfully. It emitted the existing non-blocking `[UninstallRun]` warning about `RunOnceId`.

## Scope

Stage 184 strengthens release verification for remembered administrator sessions. No production credentials or workspaces were used or changed. The existing installed 0.6.18 process was left running and untouched. `develop` and the stable ImGui implementation remain unchanged.
