# ForgeMirror 0.6.32 Qt installer verification

- Canonical version: root `VERSION` = `0.6.32`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.32.exe`.
- Size: `11,533,074` bytes.
- SHA-256: `969432C60AA1E5DEDC3F033E7AF4DE8163A9FC64519EB600C7EADBA9CAF7534E`.
- Setup ProductVersion and packaged EXE ProductVersion/FileVersion: `0.6.32`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; current-user installation (`PrivilegesRequired=lowest`).
- Payload: 28 files.

## Verification

- `build-qt.ps1 -Package`: passed; `smoke_qt` 1/1 and `smoke_core: OK`.
- Administrator auth coverage submits the password in the actual Qt login dialog with **Не выходить после перезапуска** checked, then launches three independent processes. Each restores administrator mode, retains the credential record and produces no rejected-login event.
- Profile-session regression covers trust expiry and external revocation while the session is active, including audit-write rollback and retry.
- Lifecycle directory: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage186-profile-session-20260928`. Installed 0.6.31 and updated in place to 0.6.32 using the stable AppId. EXE ProductVersion and HKCU uninstall DisplayVersion matched after each step.
- Installed 0.6.32 launched with Qt removed from `PATH` and Qt plugin overrides cleared, using a disposable workspace; exit code 0, screenshot 54,033 bytes.
- Silent uninstall exited 0, removed the test application directory and uninstall entry, and preserved a separate user-data marker.
- The pre-existing Qt process list was unchanged through lifecycle verification; the stable ImGui uninstall record remained unchanged.
- Inno Setup 6.7.3 compiled successfully. It emitted the existing non-blocking `[UninstallRun]` warning about `RunOnceId`.

## Scope

Stage 186 revalidates remembered profile trust while a process is active. Admin remembered-login behavior is covered by real-dialog and process-boundary regression tests. No production profile data or credentials were used or changed. The already running installed 0.6.18 client was left running and untouched. `develop` and the stable ImGui implementation remain unchanged.
