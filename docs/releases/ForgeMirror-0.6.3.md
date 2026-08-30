# ForgeMirror 0.6.3 Qt installer verification

- Canonical version: `VERSION` = `0.6.3`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.3.exe`.
- Installer size: `10,942,100` bytes.
- SHA-256: `9E97A93BB0F271F8F722ABF3658B50FC4304C9C26CCA59173F9F41D5056CD34A`.
- Application: `ForgeMirrorQt.exe`; EXE ProductVersion/FileVersion and `--version` all report `0.6.3`.
- Installer metadata and the uninstall registry entry report `0.6.3`.
- Stable installer identity: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`.
- Scope: per-user installation under `%LOCALAPPDATA%\Programs\ForgeMirror`; elevation is not required.
- Payload: Qt application and runtime only. No workspace, profile, photo, secret, cloud configuration or developer data is included.

## Verification performed on 2026-08-30

The installer was exercised in `Z:\CPP\ForgeMirror\build-qt\installer-test-stage38`:

1. Installed verified version `0.6.2` silently into an isolated application directory.
2. Created an application-directory marker and a separate disposable user-workspace sentinel.
3. Installed `0.6.3` over `0.6.2` with the same AppId and directory; both markers survived.
4. With `PATH` limited to Windows system directories, confirmed `ForgeMirrorQt --version` returned `ForgeMirrorQt 0.6.3` and a real one-second window smoke test returned `0` with empty stderr.
5. Confirmed EXE ProductVersion/FileVersion, Setup ProductVersion, HKCU uninstall `DisplayName=ForgeMirror 0.6.3` and `DisplayVersion=0.6.3` all matched the canonical version.
6. Ran the generated uninstaller silently. It returned `0`, removed the installed EXE and registry entry, and preserved the separate user-workspace sentinel.

The full build passed `smoke_core` and `smoke_qt`. Direct XP tests cover checked two-file rollback, restart recovery, achievement bonus calculation, blocked-profile rejection and the real Qt form. The form was rendered and inspected without clipped controls. The existing stable ImGui installation and its distinct installer identity were not modified. This Qt installer has not been published through PharosHub, so no Hub manifest was changed.
