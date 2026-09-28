# ForgeMirror 0.6.18 Qt installer verification

- Canonical version: root `VERSION` = `0.6.18`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.18.exe`.
- Size: `11,510,977` bytes.
- SHA-256: `A08EFD63794EBC47D98508E7D63A826E41EC68067B9CC150220C49DEF24B79D1`.
- Setup ProductVersion and installed EXE ProductVersion/FileVersion: `0.6.18`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; per-user install (`PrivilegesRequired=lowest`).
- Payload: 28 files.

## Verification

- `build-qt.ps1 -Package`: passed; `smoke_qt` 1/1 and `smoke_core: OK`. `smoke_qt` covers 60–100% control, legacy alpha import, Qt preset save/apply round-trip, INI persistence and main-window opacity restored at startup.
- Packaged real-window smoke: `build-qt/stage172-package-smoke-7bb27f7c586344abb49b2884f18343ba/window.png`, exit 0 with minimal Windows `PATH` and Qt plugin variables cleared; image 45,631 bytes, stderr empty.
- Isolated lifecycle: `build-qt/installer-test-stage172-1d2f1747eca248f7b92ca642f229c47c`. Installed 0.6.17, updated in place to 0.6.18 in the same dedicated `/DIR`, verified stable-AppId uninstall registry data and EXE ProductVersion/FileVersion, launched the installed app with minimal Windows `PATH` (exit 0, screenshot 45,568 bytes, stderr empty), then uninstalled. Uninstall removed the app and registry entry; a separate user-data marker remained intact.
- Inno Setup 6.7.3 compiled successfully. Existing non-blocking `[UninstallRun]` warning about missing `RunOnceId` remains.

## Scope

Qt migration checkpoint 172 carries over the stable ImGui global alpha as main-window opacity. It imports legacy `[style] alpha`, stores the value in Qt display settings and custom layout presets, and restores it at startup. Dialogs remain opaque, and the fixed palette is unchanged.
