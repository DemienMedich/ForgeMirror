# ForgeMirror 0.6.13 Qt installer verification

- Canonical version: root `VERSION` = `0.6.13`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.13.exe`.
- Size: 11,507,623 bytes.
- SHA-256: `8C8D6543CEA458BF8D0D80AFE26B8D0C77B6378174C4FF5E18D5A6D410250663`.
- Setup ProductVersion: `0.6.13`; installed application ProductVersion/FileVersion: `0.6.13`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; per-user install (`PrivilegesRequired=lowest`), no elevation.
- Previous installer used for update verification: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.12.exe`.
- Package payload inventory: 28 EXE/DLL/plugin/config files; no workspace, `meta` directory, admin settings or developer data.

## Verification

Verified on 2026-09-28. Evidence directory: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage167-789d06bc61c1487496c689e5c7e14ef3`.

1. `build-qt.ps1 -Package` completed; `smoke_qt` passed 1/1 and `smoke_core` reported OK.
2. Installed 0.6.12 into an isolated application directory; the installed EXE ProductVersion was 0.6.12.
3. Installed 0.6.13 over 0.6.12 at the same path. HKCU uninstall `DisplayVersion`, installed EXE ProductVersion and FileVersion all report 0.6.13.
4. With `PATH=C:\Windows\System32;C:\Windows` and Qt plugin environment overrides cleared, the installed real-window `--smoke-test` exited 0, created `smoke\installed-window.png` (40,891 bytes), and produced empty stderr.
5. Ran the installed uninstaller; the EXE and HKCU uninstall entry were removed. A separate user-data marker outside the application directory was unchanged.

The packaged real-window smoke also passed with the same minimal `PATH`; its screenshot is `build-qt/stage167-package-smoke-af1da38ee673430ba55e21a157f57441/window.png` (40,855 bytes). Qt UI and core smoke tests were rerun after test coverage was finalized.

## Scope

Qt migration checkpoint 167 persists application-log level/source filters in the Qt workspace and restores level filters across window restarts. See `data/meta/patch-notes/0.6.13.md`.
