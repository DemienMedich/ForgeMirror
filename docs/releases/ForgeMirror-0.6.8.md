# ForgeMirror 0.6.8 Qt installer verification

- Canonical version: root VERSION = 0.6.8.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.8.exe`.
- Size: 10,999,120 bytes.
- SHA-256: `FAD90E5910E943899B24280CD81BCD0A3E1420B666C82225A152BC39591D5F0A`.
- Application: ForgeMirrorQt.exe; EXE ProductVersion/FileVersion and --version report 0.6.8.
- Setup ProductVersion is 0.6.8 with Inno Setup trailing-space padding; compared after trimming.
- Stable AppId: {8B99E76B-4510-49D8-AE45-9DDF85EA21DC}; per-user installation, no elevation.
- Payload inspected: application and runtime EXE/DLL files only; no workspace or developer data.

## Verification on 2026-09-16

Evidence directory: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage43-20260916`.

1. Installed 0.6.7 into the isolated app directory, exit 0.
2. Created an application marker and a separate workspace sentinel.
3. Installed 0.6.8 into the same directory using the same AppId, exit 0; markers preserved.
4. Checked HKCU uninstall DisplayVersion=0.6.8 and EXE/Setup version metadata.
5. Limited PATH to Windows and System32, ran --version and the installed real-window --smoke-test. Both exited 0 with empty stderr. Inspected installed-window.png.
6. Ran the generated uninstaller, exit 0. Installed EXE and uninstall registration were removed. Workspace sentinel remained unchanged.

Both smoke_qt and smoke_core passed. A native Windows smoke_qt run also passed;
the cloud page and confirmation screenshot were inspected in build-qt/stage43-native.
The offscreen screenshot uses missing-glyph boxes on this host, so it was not used for visual acceptance.

## Stage 43 scope

Manual pull requires confirmation and executes first against a temporary workspace.
Changed files are applied atomically only after a full sibling backup and a checked recovery journal exist.
Write failure rolls back; interrupted commits recover before workspace loading.
Conflicting storage.json and malformed changed JSON abort without local writes.
Tests exercise those cases, sharing locks, overlap/disabled/no-op guards, unsafe recovery paths,
backup contents, and actual button cancellation/confirmation with data reload.

Backups are retained as sibling qt-cloud-backup-UUID directories. The journal is
meta/qt-cloud-pull.json. Backups contain local user data and remain local; no automatic pruning
or cloud upload is performed. The confirmation warns that cloud files replace local edits;
the displayed difference count covers tasks/pipeline only. JSON syntax checks do not validate
every domain schema or cross-reference. Recovery was tested using an interrupted-commit fixture,
not a physical power failure. Push, automatic sync and explicit conflict resolution remain pending.

Stable branches remain at 7306152c603ff8007200f64e63c4188510d55588.
This installer was not published through PharosHub; no Hub manifest was changed.
