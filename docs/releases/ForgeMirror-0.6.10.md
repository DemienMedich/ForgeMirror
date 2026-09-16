# ForgeMirror 0.6.10 Qt installer verification

- Canonical version: root `VERSION` = `0.6.10`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.10.exe`.
- Size: 11,016,465 bytes.
- SHA-256: `AAA50B6C36DAAD6BFB6D095B999065B9A45BC11F83FBFCCB0844BAD49FDEE861`.
- Application: `ForgeMirrorQt.exe`; EXE ProductVersion/FileVersion and `--version` report `0.6.10`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; per-user installation, no elevation.
- Payload inspected: application and runtime EXE/DLL files only; no workspace or developer data.

## Verification on 2026-09-16

Evidence directory: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage45-final-20260916`.

1. Installed 0.6.9 into an isolated application directory, exit 0.
2. Created application and separate workspace preservation markers.
3. Installed 0.6.10 into the same directory using the stable AppId, exit 0; both markers were preserved.
4. Checked HKCU uninstall `DisplayVersion=0.6.10`, installed EXE metadata and application `--version`.
5. Limited `PATH` to Windows and System32, ran `--version` and the installed real-window `--smoke-test`. Both exited 0 with empty stderr; `installed-window.png` was inspected.
6. Ran the generated uninstaller, exit 0. The installed EXE was removed and the separate workspace marker remained unchanged.

The final source passed `smoke_qt`, `smoke_core`, package deployment and native dialog inspection. The push smoke covers valid local-to-cloud replacement, compatible local/cloud snapshots, malformed or unsupported sources, a Windows sharing lock with byte-identical cloud preservation, default-cancel confirmation and the actual dialog route.

## Stage 45 scope

The comparison dialog can explicitly send one local `meta/tasks.json` or `meta/pipeline.json` to the configured cloud root. Before replacement it snapshots the local source and existing cloud target into the local `meta/updates`, rejects stale previews, symlinks/reparse traversal and overlapping roots, and commits with `QSaveFile` without direct-write fallback.

Whole-workspace push, automatic sync, background conflict decisions, `storage.json` upload, snapshot pruning and full domain-schema/cross-reference validation remain unavailable. Stable ImGui branches remain at `7306152c603ff8007200f64e63c4188510d55588`. This installer was not published through PharosHub; no Hub manifest was changed.
