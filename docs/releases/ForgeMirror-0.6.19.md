# ForgeMirror 0.6.19 Qt installer verification

- Canonical version: root `VERSION` = `0.6.19`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.19.exe`.
- Size: `11,512,507` bytes.
- SHA-256: `AF3682ACC98F61287902EDE88693B7BD835F8E476690800BE0470AD0C3EA3D06`.
- Setup ProductVersion and packaged EXE ProductVersion/FileVersion: `0.6.19`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; current-user install (`PrivilegesRequired=lowest`).
- Payload: 28 files.

## Verification

- `build-qt.ps1 -Package`: passed; `smoke_qt` 1/1 and `smoke_core: OK`.
- Layout preset tests cover Minimalism, Presentation and Compact values, preservation of unrelated fields and background image assignments, UI application and save behavior. The presets do not alter the Qt palette.
- Isolated lifecycle: `build-qt/installer-test-stage173-3a1f97aef7bc40b3bcc50921909c565e`. Installed 0.6.18, updated in place to 0.6.19 using the same dedicated `/DIR`, and verified the EXE and uninstall registry versions. The installed 0.6.19 EXE launched with `PATH` restricted to Windows directories and Qt plugin environment variables cleared (exit 0; screenshot at `build-qt/installer-test-stage173-3a1f97aef7bc40b3bcc50921909c565e/installed-window-final.png`, 45,634 bytes). Uninstall removed the application and registry key while preserving a separate user-data marker.
- Inno Setup 6.7.3 compiled the installer successfully. It emitted the existing non-blocking `[UninstallRun]` warning about `RunOnceId`.

## Scope

Stage 173 adds the stable UI's Minimalism, Presentation and Compact quick layout presets to Qt's display settings. They adjust Qt-supported layout values only and preserve the fixed palette. Minimalism also sets its reduced opacity and background defaults. The chosen values remain a draft until the user saves the settings.
