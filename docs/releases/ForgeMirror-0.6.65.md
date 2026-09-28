# ForgeMirror Qt 0.6.65 release verification

- Canonical version: root `VERSION` = `0.6.65`.
- Scope: run all six Qt administrator bulk task edits inside the shared recovery journal. Task and audit file failures restore byte-exact pre-operation state and in-memory snapshots. The release notes also clarify the scoped cloud pull/push behavior.
- Release installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.65.exe`; 11,565,324 bytes; SHA-256 `F3EE6CD3F715B81BB0A0CC5978DBFACB6CBA78A188B56FC35155296DE5EC9D27`.
- Installer FileVersion/ProductVersion: `0.6.65`; built with Inno Setup 6.7.3. Production AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}` and install mode remains per-user.
- Packaged executable: `Z:\CPP\ForgeMirror\package-qt-0.6.65\ForgeMirrorQt.exe`; FileVersion/ProductVersion `0.6.65`; SHA-256 `8BCF2EA8F15CFC72335E32A1FEB83DE8738CE579469F0C0FCEB642CC41667B93`.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.65` completed. `smoke_qt` passed 1/1 (29.47 sec); `smoke_core: OK`. The new regression injects an audit append failure after a bulk priority change and verifies exact file rollback, restored in-memory task/audit state, a cleared transaction journal and one generic warning event. The successful path also remains committed when the event observer throws.
- The ImGui compatibility target `ForgeMirrorGui` rebuilt successfully with ProductVersion/FileVersion `0.6.65`.
- Packaged Qt real-window smoke ran with `PATH=C:\Windows\System32;C:\Windows`, Qt environment overrides cleared and disposable storage. Exit code was 0; screenshot: `Z:\CPP\ForgeMirror\build-qt\runtime-verify-0.6.65-124188c9c3dc47a399e74b50f34b4a3c\window.png` (52,557 bytes).
- The installed Qt executable also launched successfully without Qt on `PATH` after both installation and in-place update. Its displayed executable version matched the installer version at each step.
- Isolated lifecycle directory: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.65-4938fde8c34748c6ae9d7f71fa607114`; temporary test AppId `{A8E3AD19-7821-4B21-8D9E-57D8C4E70CA4}` and dedicated install directory. Test installer 0.6.64 installed, then 0.6.65 updated it in place; installer exits were 0, installed-window smokes exited 0, and HKCU uninstall `DisplayVersion` matched `0.6.65`. Silent uninstall exited 0 and removed the test executable and registry entry. External workspace marker was preserved with SHA-256 `FE81F925208FDC2E59C9069ADE5FFA55E950119C3E3402F5954DABB05D056263`.
- Lifecycle testing used only a temporary AppId and install directory. It did not alter the production installation, user workspace data, or `develop`. The preserved ImGui baseline and `develop` remain unchanged.

The installer is the release deliverable; the package directory is retained for QA only. No workspace, profile, cloud configuration, or developer-local storage is included in the installer. The Qt migration remains an estimated 90% complete; the estimate is a feature-coverage assessment, not a test percentage.
