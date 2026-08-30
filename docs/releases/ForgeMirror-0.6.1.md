# ForgeMirror 0.6.1 Qt installer verification

- Canonical version: `VERSION` = `0.6.1`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.1.exe`.
- Installer size: `10,933,169` bytes.
- SHA-256: `0D97E36E05961EF44792FC1C43CCEA2B6F372524C18E34AF64A2DE03254858D2`.
- Application: `ForgeMirrorQt.exe`; EXE ProductVersion/FileVersion and `--version` all report `0.6.1`.
- Installer metadata and the uninstall registry entry report `0.6.1`.
- Stable installer identity: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`.
- Scope: per-user installation under `%LOCALAPPDATA%\Programs\ForgeMirror`; elevation is not required.
- Payload: Qt application and runtime only. No workspace, profile, photo, secret, cloud configuration or developer data is included.

## Verification performed on 2026-08-30

The installer was exercised in `Z:\CPP\ForgeMirror\build-qt\installer-test-stage36`:

1. Installed `0.6.0` silently into an isolated application directory; the installed EXE started with a disposable explicit workspace and a system-only `PATH`.
2. Created an application-directory marker and a separate user-workspace sentinel.
3. Built `0.6.1` from the final canonical `VERSION` and installed it over `0.6.0` with the same AppId and directory.
4. Confirmed the marker and user workspace survived the update, `ForgeMirrorQt --version` returned `ForgeMirrorQt 0.6.1`, the installed EXE metadata reported `0.6.1`, startup returned `0`, and stderr was empty.
5. Confirmed the HKCU uninstall entry contained `DisplayName=ForgeMirror 0.6.1`, `DisplayVersion=0.6.1`, the correct installed path and a quiet uninstaller.
6. Ran the generated uninstaller silently. It returned `0`, removed the installed EXE and registry entry, and preserved the separate user-workspace sentinel.

The existing stable ImGui installation and its distinct installer identity were not modified. This Qt installer has not been published through PharosHub, so no Hub manifest was changed.
