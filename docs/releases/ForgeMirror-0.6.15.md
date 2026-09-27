# ForgeMirror 0.6.15 Qt installer verification

- Canonical version: root `VERSION` = `0.6.15`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.15.exe`.
- Size: 11,510,631 bytes.
- SHA-256: `01E838A884127074CCA2A9E61E90D032843EDFB0CA09EF3C332B366712542B4B`.
- Setup ProductVersion and installed EXE ProductVersion/FileVersion: `0.6.15`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; per-user install (`PrivilegesRequired=lowest`).
- Previous installer for update verification: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.14.exe`.
- Package payload inventory: 28 EXE/DLL/plugin/config files across the application and Qt plugin directories; no workspace, `meta` directory, admin settings or developer data.

## Verification

Verified on 2026-09-28. Evidence directory: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage169-655fdab968954821b24c1d8eb858b141`.

1. `build-qt.ps1 -Package` completed; `smoke_qt` passed 1/1 and `smoke_core` reported OK. The UI test checks concise labels, icon presence, accessible text and keyboard hints.
2. The packaged real-window smoke exited 0 with `PATH=C:\Windows\System32;C:\Windows`, Qt plugin environment overrides cleared, empty stderr and screenshot `build-qt/stage169-package-smoke-final/window.png` (44,712 bytes).
3. Installed 0.6.14 into an isolated directory, then updated in place to 0.6.15. HKCU uninstall `DisplayVersion`, installed EXE ProductVersion and FileVersion all report 0.6.15.
4. The installed real-window smoke under the minimal `PATH` exited 0, created `smoke\installed-window.png` (45,549 bytes) and had empty stderr.
5. The uninstaller exited 0 and removed the app EXE and registry entry; a separate user-data marker outside the application directory was unchanged.

## Scope

Qt migration checkpoint 169 refines the persistent navigation using the supplied interface references while preserving the established palette. See `data/meta/patch-notes/0.6.15.md`.
