# ForgeMirror Qt 0.6.47 installer verification

- Canonical version: root `VERSION` = `0.6.47`.
- Production installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.47.exe`.
- Installer size: 11,552,591 bytes; SHA-256: `16F11F4C7798E37B1CF898BB04C34356BD0D316413E01420277A104EB7DB1BC7`.
- Packaged EXE size: 3,337,728 bytes; FileVersion/ProductVersion `0.6.47`; SHA-256: `43F94307D3D2B0C0C45D78001212AC65A5A78AA9B633830E46813ACBADCE5066`.
- Installer ProductVersion: `0.6.47`. Production installer compiled by Inno Setup 6.7.3. Production AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; per-user install and 28-file payload.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-next7` passed: `smoke_qt` 1/1 in 22.60 seconds; `smoke_core: OK`. The new regression checks verify the journaled status service reports committed and rolled-back outcomes and that a throwing observer cannot alter a committed status update.
- The production installer was compiled by `installer/build-qt-installer.ps1 -PackageDirectory .\package-qt-next7`; EXE and installer metadata both match canonical version `0.6.47`.
- Isolated lifecycle used test-only AppId `{A7D86211-6D9D-48C9-A0D8-A11BF0FE8D64}`, a separate application install directory, isolated Qt app-data directories and a disposable workspace. Test-only setup had `CloseApplications=no`; the production script retains `CloseApplications=yes`.
- Installed test version 0.6.46, created a workspace marker, then updated in place to 0.6.47. Installed EXE ProductVersion and HKCU uninstall `DisplayVersion` both reported `0.6.47`; the workspace marker survived the update.
- Installed 0.6.47 returned `ForgeMirrorQt 0.6.47` and passed the real-window `--smoke-test` with `PATH=C:\Windows\System32;C:\Windows`, Qt plugin environment overrides cleared, and isolated `LOCALAPPDATA`, `APPDATA` and workspace. Exit code 0; stderr empty. Screenshot: `Z:\CPP\ForgeMirror\build-qt\installer-lifecycle-0.6.47\installed-smoke.png` (51,588 bytes), visually inspected.
- Silent uninstall exited 0, removed the test EXE and test uninstall registry entry, and preserved both workspace and external user-data markers. Production ForgeMirrorQt PID 11144 remained running from `C:\Users\mrdem\AppData\Local\Programs\ForgeMirror\ForgeMirrorQt.exe`; its production installation was not updated or modified.

## Scope

Stage 202 moves journaled single-task status outcome telemetry into an optional exception-isolated core observer, removing the duplicate Qt-side event. It does not close the race between a stale-task comparison and atomic replacement, coordinate live external writers in multi-file transactions, complete telemetry for every non-UI core workflow, or replace hands-on NVDA/JAWS interaction testing. Functional parity remains an expert estimate of about 90%, not a measured code/test percentage. Stable ImGui and `develop` remain unchanged.
