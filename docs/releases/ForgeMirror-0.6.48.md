# ForgeMirror Qt 0.6.48 installer verification

- Canonical version: root `VERSION` = `0.6.48`.
- Production installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.48.exe`.
- Installer size: 11,553,614 bytes; SHA-256: `C4D1793D4982A7A95072E0894B120450B8ED76EE9333865F29FACA29D884ADFC`.
- Packaged EXE size: 3,339,264 bytes; FileVersion/ProductVersion `0.6.48`; SHA-256: `B52D96900B1F3445BA606BA2E23480E1DBE2CA449C0211C9A8ACFE98EB0E28AD`.
- Installer ProductVersion: `0.6.48`. Production installer compiled by Inno Setup 6.7.3. Production AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; per-user install and 28-file payload.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-next8` passed: `smoke_qt` 1/1 in 22.38 seconds and `smoke_core: OK`. The new core smoke covers profile-creation rejection and success, verifies no name/ID/login/one-time password enters the event, and verifies observer exceptions cannot change successful creation.
- The production installer was compiled by `installer/build-qt-installer.ps1 -PackageDirectory .\package-qt-next8`; packaged EXE and installer metadata match canonical version `0.6.48`.
- Isolated lifecycle used test-only AppId `{A7D86211-6D9D-48C9-A0D8-A11BF0FE8D64}`, a separate per-user application directory, isolated Qt app-data directories, and disposable workspace. Test-only setup used `CloseApplications=no`; the production script retains `CloseApplications=yes`.
- Installed test version 0.6.47, created workspace and external data markers, then updated in place to 0.6.48. Installed EXE ProductVersion and HKCU uninstall `DisplayVersion` both reported `0.6.48`; the workspace marker survived the update.
- Installed 0.6.48 returned `ForgeMirrorQt 0.6.48` and passed real-window `--smoke-test` with `PATH=C:\Windows\System32;C:\Windows`, Qt plugin environment overrides cleared, and isolated `LOCALAPPDATA`, `APPDATA`, and workspace. Exit code 0; stderr empty. Screenshot: `Z:\CPP\ForgeMirror\build-qt\installer-lifecycle-0.6.48\installed-smoke.png` (51,716 bytes), visually inspected.
- Silent uninstall exited 0, removed the test EXE and test uninstall registry entry, and preserved both workspace and external data markers. The production install and its user workspace were not used by this lifecycle.

## Scope

Stage 203 adds privacy-safe profile-creation telemetry through an optional exception-isolated core observer, leaving non-Qt callers unchanged. Other core operations remain uninstrumented, and this does not close the stale-task compare/replace race, coordinate external writers during multi-file transactions, or replace hands-on NVDA/JAWS interaction tests. Functional parity remains an expert estimate of about 90%, not a measured code/test percentage. Stable ImGui and `develop` remain unchanged.
