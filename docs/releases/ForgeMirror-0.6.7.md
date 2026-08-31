# ForgeMirror 0.6.7 Qt installer verification

- Canonical version: `VERSION` = `0.6.7`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.7.exe`.
- Installer size: `10,968,383` bytes.
- SHA-256: `694D74877E6D453AEC49A78747779BDD0D7089BE2BD6EE00CAEF9851BC2BD631`.
- Application: `ForgeMirrorQt.exe`; EXE ProductVersion/FileVersion and `--version` all report `0.6.7`.
- Installer metadata and the uninstall registry entry report `0.6.7`.
- Stable installer identity: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`.
- Scope: per-user installation under `%LOCALAPPDATA%\Programs\ForgeMirror`; elevation is not required.
- Payload: Qt application and runtime only. No workspace, cloud folder, profile, photo, secret, cloud configuration or developer data is included.

## Verification performed on 2026-08-31

The installer was exercised in `Z:\CPP\ForgeMirror\build-qt\installer-test-stage42`:

1. Installed verified version `0.6.6` silently into an isolated application directory.
2. Created an application-directory marker and a separate disposable user-workspace sentinel.
3. Installed `0.6.7` over `0.6.6` with the same AppId and directory; both markers survived.
4. With `PATH` limited to Windows system directories, confirmed `ForgeMirrorQt --version` returned `ForgeMirrorQt 0.6.7` with empty stderr and a real window smoke test returned `0`.
5. Confirmed EXE ProductVersion/FileVersion, Setup ProductVersion and the HKCU uninstall `DisplayVersion=0.6.7` all matched the canonical version.
6. Ran the generated uninstaller silently. It returned `0`, removed the installed EXE and registry entry, and preserved the separate user-workspace sentinel.

The full build passed `smoke_core` and `smoke_qt`. Cloud-configuration tests cover a Cyrillic external path, compatible round-trip persistence, overlap rejection, a real Windows sharing lock with byte-identical preservation and the production readiness page/dialog route. The settings form was rendered and inspected without clipped controls. Qt does not invoke pull, push, wallet upload, release download or conflict resolution in this release. The stable ImGui branches remain at `7306152c603ff8007200f64e63c4188510d55588` and were not modified. This Qt installer has not been published through PharosHub, so no Hub manifest was changed.
