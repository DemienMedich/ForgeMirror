# ForgeMirror 0.6.6 Qt installer verification

- Canonical version: `VERSION` = `0.6.6`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.6.exe`.
- Installer size: `10,956,962` bytes.
- SHA-256: `042FFBAD1C6E1720CED9B2D87F35EA9E54A4C83C03986B80EAA4543A69244980`.
- Application: `ForgeMirrorQt.exe`; EXE ProductVersion/FileVersion and `--version` all report `0.6.6`.
- Installer metadata and the uninstall registry entry report `0.6.6`.
- Stable installer identity: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`.
- Scope: per-user installation under `%LOCALAPPDATA%\Programs\ForgeMirror`; elevation is not required.
- Payload: Qt application and runtime only. No workspace, profile, banner content, photo, secret, cloud configuration or developer data is included.

## Verification performed on 2026-08-31

The installer was exercised in `Z:\CPP\ForgeMirror\build-qt\installer-test-stage41`:

1. Installed verified version `0.6.5` silently into an isolated application directory.
2. Created an application-directory marker and a separate disposable user-workspace sentinel.
3. Installed `0.6.6` over `0.6.5` with the same AppId and directory; both markers survived.
4. With `PATH` limited to Windows system directories, confirmed `ForgeMirrorQt --version` returned `ForgeMirrorQt 0.6.6` with empty stderr and a real window smoke test returned `0`.
5. Confirmed EXE ProductVersion/FileVersion, Setup ProductVersion and the HKCU uninstall `DisplayVersion=0.6.6` all matched the canonical version.
6. Ran the generated uninstaller silently. It returned `0`, removed the installed EXE and registry entry, and preserved the separate user-workspace sentinel.

The full build passed `smoke_core` and `smoke_qt`. Banner tests cover Cyrillic edit/delete, canonical reload validation, a real Windows sharing lock with byte-identical preservation, administrator-only production navigation and immediate strip refresh. The editor and banner page were rendered and inspected without clipped controls. `meta/banner.json` is staged and replaced through `QSaveFile` with direct-write fallback disabled; symbolic-link targets and parent directories are rejected. The stable ImGui branches remain at `7306152c603ff8007200f64e63c4188510d55588` and were not modified. This Qt installer has not been published through PharosHub, so no Hub manifest was changed.
