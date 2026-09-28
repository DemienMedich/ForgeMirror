# ForgeMirror Qt 0.6.49 installer verification

- Canonical version: root `VERSION` = `0.6.49`.
- Production installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.49.exe`.
- Installer size: 11,553,660 bytes; SHA-256: `AF38A7C7065BD45E099948EF5C3320F78A1E81F374CE9F5F287C972C21733B09`.
- Packaged EXE size: 3,339,776 bytes; FileVersion/ProductVersion `0.6.49`; SHA-256: `6324B340F1D63F5275B6099C7DB9DC4BFDCF0089E78B0B11771FF41E6A78FE20`.
- Installer ProductVersion: `0.6.49`. Production installer compiled by Inno Setup 6.7.3. Production AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; per-user install and 28-file payload.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-next9` passed: `smoke_qt` 1/1 in 22.37 seconds and `smoke_core: OK`. Profile dialog smoke verifies failed archive rollback plus successful archive and restore outcomes through the core event sink; messages contain no profile ID.
- Production setup was compiled by `installer/build-qt-installer.ps1 -PackageDirectory .\package-qt-next9`. Packaged EXE and installer version metadata match root `VERSION`.
- Isolated lifecycle used test-only AppId `{A7D86211-6D9D-48C9-A0D8-A11BF0FE8D64}`, a separate per-user application directory, isolated Qt app-data directories and a disposable workspace. Test setup used `CloseApplications=no`; the production installer retains `CloseApplications=yes`.
- Installed 0.6.48, created workspace and external user-data markers, then updated in place to 0.6.49. Installed EXE ProductVersion and HKCU uninstall `DisplayVersion` both reported `0.6.49`; the workspace marker survived update.
- Installed 0.6.49 returned `ForgeMirrorQt 0.6.49` and passed real-window `--smoke-test` with `PATH=C:\Windows\System32;C:\Windows`, Qt plugin environment overrides cleared, and isolated `LOCALAPPDATA`, `APPDATA` and workspace. Exit code 0; stderr empty. Screenshot: `Z:\CPP\ForgeMirror\build-qt\installer-lifecycle-0.6.49\installed-smoke.png` (51,765 bytes).
- Silent uninstall exited 0, removed the test EXE and test uninstall registry entry, and preserved both workspace and external user-data markers. Production installation and workspace were not used by this lifecycle.

## Scope

Stage 204 adds privacy-safe commit/rollback telemetry to journaled profile archive and restore operations, using an optional exception-isolated sink. Remaining gaps include other non-UI core events, the optimistic task compare/replace race, concurrent external writers during multi-file transactions and hands-on NVDA/JAWS testing. Functional parity remains an expert estimate of about 90%, not a measured code/test percentage. Stable ImGui and `develop` remain unchanged.
