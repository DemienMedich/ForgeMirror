# ForgeMirror 0.6.9 Qt installer verification

- Canonical version: root `VERSION` = `0.6.9`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.9.exe`.
- Size: 11,012,986 bytes.
- SHA-256: `83163AC7063B20613CA2A5722C474C169B2406E4660A4560C61CA0E00CE2259A`.
- Application: `ForgeMirrorQt.exe`; EXE ProductVersion/FileVersion and `--version` report `0.6.9`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; per-user installation, no elevation.
- Payload inspected: application and runtime EXE/DLL files only; no workspace or developer data.

## Verification on 2026-09-16

Evidence directory: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage44-final-20260916`.

1. Installed 0.6.8 into the isolated application directory, exit 0.
2. Created application and separate workspace preservation markers.
3. Installed the final rebuilt 0.6.9 into the same directory using the same AppId, exit 0; both markers were preserved.
4. Checked HKCU uninstall `DisplayVersion=0.6.9`, installed EXE metadata and application `--version`.
5. Limited `PATH` to Windows and System32, ran `--version` and the installed real-window `--smoke-test`. Both exited 0 with empty stderr; `installed-window.png` was inspected.
6. Ran the generated uninstaller, exit 0. The installed EXE was removed and the separate workspace marker remained unchanged.

The final source passed `smoke_qt`, `smoke_core`, package deployment and native conflict-dialog inspection. The conflict smoke covers malformed JSON, foreign sources, Windows sharing locks, cancellation, confirmation, cloud-to-local application and snapshot restore.

## Stage 44 scope

The Qt cloud page can compare local and cloud `tasks.json` or `pipeline.json`, accept the configured cloud file into the isolated local workspace, and restore compatible local snapshots. Every write requires confirmation, validates source containment and reparse-point safety, re-reads the source, snapshots the current local file and uses `QSaveFile` without direct-write fallback.

Cloud files are never modified. Local-to-cloud push, automatic sync, `storage.json` conflict resolution, snapshot pruning and full domain-schema/cross-reference validation remain unavailable. The stable ImGui branches remain at `7306152c603ff8007200f64e63c4188510d55588`. This installer was not published through PharosHub; no Hub manifest was changed.
