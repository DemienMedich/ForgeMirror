# ForgeMirror 0.6.5 Qt installer verification

- Canonical version: `VERSION` = `0.6.5`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.5.exe`.
- Installer size: `10,953,176` bytes.
- SHA-256: `CA6C1A7781AED89FB9249BBAC3DBB17FB65205DEB0D8EF2EC5E5D747E8942AA0`.
- Application: `ForgeMirrorQt.exe`; EXE ProductVersion/FileVersion and `--version` all report `0.6.5`.
- Installer metadata and the uninstall registry entry report `0.6.5`.
- Stable installer identity: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`.
- Scope: per-user installation under `%LOCALAPPDATA%\Programs\ForgeMirror`; elevation is not required.
- Payload: Qt application and runtime only. No workspace, profile, shortcut target, photo, secret, cloud configuration or developer data is included.

## Verification performed on 2026-08-31

The upgrade path was exercised in `Z:\CPP\ForgeMirror\build-qt\installer-test-stage40`, and the final rebuilt installer was exercised in `Z:\CPP\ForgeMirror\build-qt\installer-test-stage40-final2`:

1. Installed verified version `0.6.4` silently into an isolated application directory.
2. Created an application-directory marker and a separate disposable user-workspace sentinel.
3. Installed `0.6.5` over `0.6.4` with the same AppId and directory; both markers survived.
4. With `PATH` limited to Windows system directories, confirmed `ForgeMirrorQt --version` returned `ForgeMirrorQt 0.6.5` with empty stderr and a real window smoke test returned `0`.
5. Confirmed EXE ProductVersion/FileVersion, Setup ProductVersion, HKCU uninstall `DisplayName=ForgeMirror 0.6.5` and `DisplayVersion=0.6.5` all matched the canonical version.
6. Ran the generated uninstaller silently. It returned `0`, removed the installed EXE and registry entry, and preserved the separate user-workspace sentinel.
7. Repeated final-artifact installation, isolated startup and removal after the UTF-8 path and symbolic-link storage guards were added; all returned `0` and the workspace sentinel survived.

The full build passed `smoke_core` and `smoke_qt`. Shortcut tests cover add/reload/reorder, injected atomic-write failure with byte-identical rollback, non-administrator access to the production page/dialog, target preservation after record deletion and availability checks. The dialog and page were rendered and inspected without clipped controls. Shortcut targets are opened through `QDesktopServices`; no stored path is executed as a shell command. The stable ImGui branches remain at `7306152c603ff8007200f64e63c4188510d55588` and were not modified. This Qt installer has not been published through PharosHub, so no Hub manifest was changed.
