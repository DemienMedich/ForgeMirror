# ForgeMirror 0.6.4 Qt installer verification

- Canonical version: `VERSION` = `0.6.4`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.4.exe`.
- Installer size: `10,946,524` bytes.
- SHA-256: `1F8EC263D626F8A814E0EFC99E9BB488040235B1C9550A717846EB53A7375543`.
- Application: `ForgeMirrorQt.exe`; EXE ProductVersion/FileVersion and `--version` all report `0.6.4`.
- Installer metadata and the uninstall registry entry report `0.6.4`.
- Stable installer identity: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`.
- Scope: per-user installation under `%LOCALAPPDATA%\Programs\ForgeMirror`; elevation is not required.
- Payload: Qt application and runtime only. No workspace, profile, photo, secret, cloud configuration or developer data is included.

## Verification performed on 2026-08-31

The upgrade path was exercised in `Z:\CPP\ForgeMirror\build-qt\installer-test-stage39` and the final rebuilt installer was exercised in `Z:\CPP\ForgeMirror\build-qt\installer-test-stage39-final`:

1. Installed verified version `0.6.3` silently into an isolated application directory.
2. Created an application-directory marker and a separate disposable user-workspace sentinel.
3. Installed `0.6.4` over `0.6.3` with the same AppId and directory; both markers survived.
4. Reinstalled the final rebuilt `0.6.4` installer after the visual layout correction and confirmed installation returned `0`.
5. With `PATH` limited to Windows system directories, confirmed `ForgeMirrorQt --version` returned `ForgeMirrorQt 0.6.4` with empty stderr and a real window smoke test returned `0`.
6. Confirmed EXE ProductVersion/FileVersion, Setup ProductVersion, HKCU uninstall `DisplayName=ForgeMirror 0.6.4` and `DisplayVersion=0.6.4` all matched the canonical version.
7. Ran the generated uninstaller silently. It returned `0`, removed the installed EXE and registry entry, and preserved the separate user-workspace sentinel.

The full build passed `smoke_core` and `smoke_qt`. Storage-vault tests cover canonical round-trip persistence, balance and journal preservation, weekday/reward settings, Windows sharing-lock rejection and the real Qt form. The form was rendered and inspected after arranging weekday controls into two rows; no controls are clipped. The stable ImGui branches remain at `7306152c603ff8007200f64e63c4188510d55588` and were not modified. This Qt installer has not been published through PharosHub, so no Hub manifest was changed.
