# ForgeMirror 0.6.16 Qt installer verification

- Canonical version: root `VERSION` = `0.6.16`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.16.exe`.
- Size: `11,510,446` bytes.
- SHA-256: `1FE6D2FC8A2A169A0F67BA68491B38BC7F01C91EE3C9028654F5B0A0E38E99DA`.
- Setup ProductVersion and installed EXE ProductVersion/FileVersion: `0.6.16`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; per-user install (`PrivilegesRequired=lowest`).
- Previous installer for update verification: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.15.exe`.
- Payload: 28 files.

## Verification

- `build-qt.ps1 -Package`: passed; `smoke_qt` 1/1 passed and `smoke_core: OK`.
- Packaged real-window smoke: exit 0 with `PATH=C:\Windows\System32;C:\Windows`, Qt plugin environment variables cleared; screenshot 44,616 bytes, stderr empty.
- Isolated installer lifecycle passed in `Z:\CPP\ForgeMirror\build-qt\installer-test-stage170-final-58d783adca414cceaf72b5a42f19726c`: installed 0.6.15, updated in place to 0.6.16 in the same dedicated `/DIR`, checked EXE metadata, ran the installed app with minimal Windows `PATH` (exit 0, screenshot 45,699 bytes, stderr empty), then uninstalled. The uninstall removed the executable and registry entry. A separate user-data marker remained intact.
- The installer emitted a non-blocking Inno Setup warning for the existing `[UninstallRun]` `RunOnceId`; no installer compile errors occurred.

## Scope

Qt migration checkpoint 170 classifies safe outcomes for rejected/successful/restored/ended administrator sessions, storage-health report export and approved stray cleanup, and cloud-release download/launch in the administrator-only Core audit source. Authentication records never contain entered passwords; cleanup records omit paths and filenames. The Core audit source label now matches its contents. See `data/meta/patch-notes/0.6.16.md`.
