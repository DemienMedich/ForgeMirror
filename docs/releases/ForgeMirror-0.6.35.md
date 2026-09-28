# ForgeMirror 0.6.35 Qt installer verification

- Canonical version: root `VERSION` = `0.6.35`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.35.exe`.
- Size: `11,539,243` bytes.
- SHA-256: `761A62498BC3AD30CA78CBF41F9CC1BD36121213741EEB6B51E40B901CF40F35`.
- Setup ProductVersion and packaged EXE ProductVersion/FileVersion: `0.6.35`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; current-user installation (`PrivilegesRequired=lowest`).
- Payload: 28 files.

## Verification

- `build-qt.ps1 -Package`: passed; `smoke_qt` 1/1 and `smoke_core: OK`.
- Core regression selects and reads a profile, externally replaces the profile INI, reselects the same active profile and attempts a stale save. Save is rejected and the replacement bytes remain identical. Re-selecting an already active profile retains the snapshot, token and queue state.
- Profile writes use unique sibling temporary files and an atomic replacement operation. The destination is never deleted as a fallback before rename.
- Inno Setup 6.7.3 compiled successfully. It emitted the existing non-blocking `[UninstallRun]` warning about `RunOnceId`.
- Isolated lifecycle directory: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage189-release-verify-20260928`. Installed 0.6.34, then updated in place to 0.6.35 under the stable AppId. EXE ProductVersion and HKCU uninstall DisplayVersion matched after both steps.
- Installed 0.6.35 launched with Qt removed from `PATH` and plugin overrides cleared, using a disposable workspace; exit code 0, screenshot 53,853 bytes.
- Silent uninstall exited 0, removed the test application directory and uninstall entry, and preserved a separate user-data marker.
- The pre-existing Qt process list was unchanged through lifecycle verification; the stable ImGui uninstall record remained unchanged.

## Scope

Stage 189 rejects stale single-profile writes and avoids the old delete-then-rename failure window. It does not make a multi-file workspace transaction atomic against a non-cooperating external client. No production workspace or credentials were used or modified. The already running installed 0.6.18 process was left running and untouched. `develop` and the stable ImGui implementation remain unchanged.
