# ForgeMirror 0.6.11 Qt installer verification

- Canonical version: root `VERSION` = `0.6.11`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.11.exe`.
- Size: 11,020,846 bytes.
- SHA-256: `D7DF3DF077B449BC6CDA1C349482AA90FBC89548B2CFD4F41B452F11C53E9921`.
- Application: `ForgeMirrorQt.exe`; EXE ProductVersion/FileVersion and `--version` report `0.6.11`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; per-user installation, no elevation.
- Payload inspected: application and runtime EXE/DLL files only; no workspace or developer data.

## Verification on 2026-09-16

Evidence directory: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage46-final-20260916`.

1. Installed 0.6.10 into an isolated application directory, exit 0.
2. Created application and separate workspace preservation markers.
3. Installed 0.6.11 into the same directory using the stable AppId, exit 0; both markers were preserved.
4. Checked HKCU uninstall `DisplayVersion=0.6.11`, installed EXE metadata and application `--version`.
5. Limited `PATH` to Windows and System32, ran `--version` and the installed real-window `--smoke-test`. Both exited 0 with empty stderr; `installed-window.png` was inspected.
6. Ran the generated uninstaller, exit 0. The installed EXE was removed and the separate workspace marker remained unchanged.

The final source passed `smoke_qt`, `smoke_core`, package deployment and native storage-dialog inspection. Tests cover choosing either complete wallet version, two local snapshots, malformed JSON, mismatched canonical `content_hash`, Windows sharing locks with byte-identical target preservation, default-cancel confirmation and the main-window action.

## Stage 46 scope

An administrator can resolve differing local/cloud `meta/storage.json` files by selecting one complete wallet. The UI compares balance, currency, revision, update time, journal size and paths. It never merges or adds balances or journal entries.

Both originals are saved locally before replacement. JSON structure and the canonical vault content hash are validated; roots, reparse traversal and stale previews are rejected; `QSaveFile` commits without direct-write fallback. Automatic resolution, whole-workspace push, automatic sync and snapshot pruning remain unavailable. Stable ImGui branches remain at `7306152c603ff8007200f64e63c4188510d55588`. This installer was not published through PharosHub; no Hub manifest was changed.
