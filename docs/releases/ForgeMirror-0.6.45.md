# ForgeMirror Qt 0.6.45 installer verification

- Canonical version: root `VERSION` = `0.6.45`.
- Production installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.45.exe`.
- Installer size: 11,551,806 bytes; SHA-256: `8A5A1FF58808FAFAAA9EE67E0D1D35274E20FF99E5CAD4CC8C9299818B71F507`.
- Installer ProductVersion and packaged EXE FileVersion/ProductVersion: `0.6.45`.
- Packaged EXE size: 3,331,584 bytes; SHA-256: `8742EC6094C49197FD31B82D163E736BC7BEE792CADB925131C020AE17A000CA`.
- Inno Setup 6.7.3 compiled the production installer. The production AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; the installer is per-user and retains the existing default install location.
- Payload: 28 files.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-next5` completed; `smoke_qt` passed 1/1 and `smoke_core: OK`. A separate rerun passed `smoke_qt` in 22.91 seconds and `smoke_core: OK`.
- `git diff --check` passed.
- Portable package `--version` and process-boundary administrator-authentication tests pass. The authentication regression enters through the real Qt dialog with the remember option checked, then starts three independent processes; each restores the administrator session without a rejected-login event.
- Isolated installer lifecycle used test-only AppId `{B5DE222B-B943-4062-96A2-741B7235F1F6}` under `Z:\CPP\ForgeMirror\build-qt\installer-lifecycle-0.6.45`. It installed 0.6.44, updated in place to 0.6.45 in the same directory, and matched EXE ProductVersion and uninstall `DisplayVersion` at both versions.
- The installed 0.6.45 EXE returned version `0.6.45` and completed a real-window startup smoke with `PATH=C:\Windows\System32;C:\Windows`, Qt-related environment variables removed, and isolated `LOCALAPPDATA`, `APPDATA`, and workspace. Exit code was 0; stderr was empty. Screenshot: `Z:\CPP\ForgeMirror\build-qt\installer-lifecycle-0.6.45\installed-smoke.png` (51,587 bytes).
- Silent uninstall exited 0, removed the test executable and uninstall registration, and preserved a marker in a separate user-data directory. The test used a disposable AppId, install directory, environment and workspace; the production registration and workspace were not modified.
- The existing production install remains ForgeMirror `0.6.37`; its executable SHA-256 after the isolated test was `0E99B8983AEC16FACC77C33636F5F1840C84C8711EED247375E5FBB1492C6625`. It was not updated or launched during this verification.

## Scope

Version 0.6.45 adds optimistic stale-task-snapshot rejection. This protects against external changes observed before the save comparison; it is not an inter-process lock and cannot close the race between comparison and replacement. Coordinated external writers, live external writes during multi-file transactions, and hands-on NVDA/JAWS interaction remain open migration work. Functional migration estimate remains about 90%, not a measured test or code percentage. Stable ImGui and `develop` are unchanged.
