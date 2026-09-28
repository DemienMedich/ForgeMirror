# ForgeMirror 0.6.34 Qt installer verification

- Canonical version: root `VERSION` = `0.6.34`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.34.exe`.
- Size: `11,535,988` bytes.
- SHA-256: `190B9B99A5771F021AA4D7639AA541B7DCC793E434296FDD9091D61124275E1E`.
- Setup ProductVersion and packaged EXE ProductVersion/FileVersion: `0.6.34`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; current-user installation (`PrivilegesRequired=lowest`).
- Payload: 28 files.

## Verification

- `build-qt.ps1 -Package`: passed; `smoke_qt` 1/1 and `smoke_core: OK`.
- Report regression covers creation-month rows for current and previous comparison cohorts, metrics, **Без даты** for legacy tasks, details, CSV export and persisted grouping choice. Existing report grouping indexes remain stable.
- Inno Setup 6.7.3 compiled successfully. It emitted the existing non-blocking `[UninstallRun]` warning about `RunOnceId`.
- Isolated lifecycle directory: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage188-final-20260928`. Installed 0.6.33, then updated in place to 0.6.34 under the stable AppId. EXE ProductVersion and HKCU uninstall DisplayVersion matched after both steps.
- Installed 0.6.34 launched with Qt removed from `PATH` and plugin overrides cleared, using a disposable workspace; exit code 0, screenshot 53,822 bytes.
- Silent uninstall exited 0, removed the test application directory and uninstall entry, and preserved a separate user-data marker.
- The pre-existing Qt process list was unchanged through lifecycle verification; the stable ImGui uninstall record remained unchanged.

## Scope

Stage 188 adds a task creation-month report grouping and keeps the fixed palette unchanged. No production workspace or credentials were used or modified. The already running installed 0.6.18 process was left running and untouched. `develop` and the stable ImGui implementation remain unchanged.
