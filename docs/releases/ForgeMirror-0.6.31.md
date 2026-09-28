# ForgeMirror 0.6.31 Qt installer verification

- Canonical version: root `VERSION` = `0.6.31`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.31.exe`.
- Size: `11,532,317` bytes.
- SHA-256: `40FBBA83185DA453A5FE58B163D4096C2F82EFFA2F68C55D3E62C9B76AA3C798`.
- Setup ProductVersion and packaged EXE ProductVersion/FileVersion: `0.6.31`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; current-user installation (`PrivilegesRequired=lowest`).
- Payload: 28 files.

## Verification

- `build-qt.ps1 -Package`: passed; `smoke_qt` 1/1, including remembered administrator login through the real dialog and three separate process restarts; `smoke_core: OK`.
- Geometry tests cover migration of all ten ImGui style metrics from `[style]` and legacy layout presets, custom Qt preset round-trip, built-in preset preservation, persisted display settings, stylesheet application and unchanged palette.
- Lifecycle directory: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage185-geometry-final-20260928`. Installed 0.6.30 and updated in place to 0.6.31 using the stable AppId. After both steps EXE ProductVersion and HKCU uninstall DisplayVersion matched; install location remained isolated.
- Installed 0.6.31 launched with Qt removed from `PATH` and plugin environment overrides cleared, using a disposable workspace; exit code 0, screenshot 53,666 bytes.
- Silent uninstall exited 0, removed the test application directory and uninstall entry, and preserved a separate user-data marker.
- The pre-existing Qt process list was unchanged through the lifecycle test; the stable ImGui uninstall record remained unchanged.
- Inno Setup 6.7.3 compiled successfully. It emitted the existing non-blocking `[UninstallRun]` warning about `RunOnceId`.

## Scope

Stage 185 brings legacy ImGui layout geometry controls into Qt while preserving the fixed palette. No production profile data or credentials were used or changed. The already running 0.6.18 client was left running and untouched. `develop` and the stable ImGui implementation remain unchanged.
