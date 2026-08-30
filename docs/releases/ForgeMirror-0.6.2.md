# ForgeMirror 0.6.2 Qt installer verification

- Canonical version: `VERSION` = `0.6.2`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.2.exe`.
- Installer size: `10,937,790` bytes.
- SHA-256: `A9B94DAA47A68620D284452625AFE5BEE43D138C0F494522C05B8709E835DD25`.
- Application: `ForgeMirrorQt.exe`; EXE ProductVersion/FileVersion and `--version` all report `0.6.2`.
- Installer metadata and the uninstall registry entry report `0.6.2`.
- Stable installer identity: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`.
- Scope: per-user installation under `%LOCALAPPDATA%\Programs\ForgeMirror`; elevation is not required.
- Payload: Qt application and runtime only. No workspace, profile, photo, secret, cloud configuration or developer data is included.

## Verification performed on 2026-08-30

The installer was exercised in `Z:\CPP\ForgeMirror\build-qt\installer-test-stage37b`:

1. Installed the verified `0.6.1` installer silently into an isolated application directory.
2. Created an application-directory marker and a separate disposable user-workspace sentinel.
3. Installed `0.6.2` over `0.6.1` with the same AppId and directory; both markers survived.
4. With `PATH` limited to Windows system directories, confirmed `ForgeMirrorQt --version` returned `ForgeMirrorQt 0.6.2` and a real one-second window smoke test returned `0` with empty stderr.
5. Confirmed EXE ProductVersion/FileVersion, Setup ProductVersion, HKCU uninstall `DisplayName=ForgeMirror 0.6.2` and `DisplayVersion=0.6.2` all matched the canonical version.
6. Ran the generated uninstaller silently. It returned `0`, removed the installed EXE and registry entry, and preserved the separate user-workspace sentinel.

The full build passed `smoke_core` and `smoke_qt`, including partial-write rollback, restart recovery, active/archive profile recalculation and the production confirmation action. The existing stable ImGui installation and its distinct installer identity were not modified. This Qt installer has not been published through PharosHub, so no Hub manifest was changed.
