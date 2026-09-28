# ForgeMirror 0.6.17 Qt installer verification

- Canonical version: root `VERSION` = `0.6.17`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.17.exe`.
- Size: `11,509,454` bytes.
- SHA-256: `A350D3A49E883985E708C3B428FB46DC65DC1A4EBA3BC159346832B525A45C9D`.
- Setup ProductVersion and installed EXE ProductVersion/FileVersion: `0.6.17`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; per-user install (`PrivilegesRequired=lowest`).
- Payload: 28 files.

## Verification

- `build-qt.ps1 -Package`: passed; `smoke_qt` 1/1 passed, including the About dialog's product copy, author credits and canonical version; `smoke_core: OK`.
- Packaged real-window smoke: `build-qt/stage171-package-smoke-77b8d2373b9b4a48b45cd06a66b3369f/window.png`, exit 0 with `PATH=C:\Windows\System32;C:\Windows` and Qt plugin variables cleared; image 45,698 bytes, stderr empty.
- Isolated lifecycle: `build-qt/installer-test-stage171-f20825a7a03d4710ae901b3e00aec84d`. Installed 0.6.16 and updated in place to 0.6.17 using the same dedicated `/DIR`; verified AppId uninstall registry entry and EXE ProductVersion/FileVersion. Installed real-window smoke exited 0 with minimal Windows `PATH`, screenshot 45,698 bytes and empty stderr. Uninstall removed the EXE and registry entry. A separate user-data marker remained intact.
- Inno Setup 6.7.3 compiled successfully. It emitted the existing non-blocking warning that the `[UninstallRun]` entry has no `RunOnceId`.

## Scope

Qt migration checkpoint 171 carries over the stable program-information screen's description, author credits and version. The dialog retains the Qt migration status, separate-workspace boundary, update capabilities and documentation pointer. No palette, ImGui or `develop` changes are included.
