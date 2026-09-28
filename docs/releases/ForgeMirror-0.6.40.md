# ForgeMirror Qt 0.6.40 installer verification

- Canonical version: root `VERSION` = `0.6.40`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.40.exe`.
- Size: `11,543,898` bytes.
- SHA-256: `72DCD679BD0D8D9DB9A5751A989F94A9CCBA395FB08CA65DF413E2F590F65147`.
- Installer FileVersion/ProductVersion and packaged EXE FileVersion/ProductVersion: `0.6.40`.
- Production AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; per-user installation (`PrivilegesRequired=lowest`).
- Payload: 28 files (31,805,340 bytes in the packaged directory).
- Compiler: Inno Setup 6.7.3; successful compile without warnings.

## Verification

- `build-qt.ps1 -Package` passed: `smoke_qt` 1/1 and `smoke_core: OK`.
- The accessibility audit now inspects visible interactive controls through `QAccessible` in the main window, administrator login, display settings, expanded geometry settings and nested background settings. It found and fixed missing accessible names/descriptions on the built-in layout selector and background tile-scale selector. The admin remember-session checkbox exposes both checkable and checked state after activation; the existing test also verifies the setting survives the UI login/restart flow. An external NVDA/JAWS run was not performed.
- Isolated per-user lifecycle used test AppId `{3824D636-F321-42C6-B4E8-BCA3950CFC04}` under `build-qt/installer-lifecycle-0.6.40-93bb396707d74ebd8fabfcec57d2e6c1/isolated-install`: version 0.6.39 installed first, then upgraded in place to 0.6.40. The uninstall entry and installed EXE both reported `0.6.40`.
- Installed EXE returned `ForgeMirrorQt 0.6.40` and completed startup smoke (`--smoke-test --screenshot`) with `PATH=C:\Windows\System32;C:\Windows`, no Qt developer path. Screenshot: `build-qt/installer-lifecycle-0.6.40-93bb396707d74ebd8fabfcec57d2e6c1/startup.png` (53,334 bytes).
- Silent uninstall removed the EXE, install directory and uninstall registration. A user-data marker in the separate workspace remained. Production 0.6.37 installation, production workspace, stable ImGui installation and `develop` were not modified by the lifecycle test.

## Scope

Stage 195 extends accessibility coverage to settings and administrator-login dialogs and names two previously unnamed controls. Estimated functional Qt migration remains about 90%; full screen-reader interaction across remaining complex dialogs, non-UI core telemetry, and concurrent external writes during multi-file transactions remain open.
