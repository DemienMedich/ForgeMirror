# ForgeMirror 0.6.14 Qt installer verification

- Canonical version: root `VERSION` = `0.6.14`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.14.exe`.
- Size: 11,508,099 bytes.
- SHA-256: `9767C2B996C836514101E068FFF4E1A80D595CD22B8C912078DE040E1B11AF1A`.
- Setup ProductVersion and installed EXE ProductVersion/FileVersion: `0.6.14`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; per-user install (`PrivilegesRequired=lowest`).
- Previous installer for update verification: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.13.exe`.

## Verification

Verified on 2026-09-28. Evidence directory: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage168-e293109398624d069d6c98445d091300`.

1. `build-qt.ps1 -Package` completed; `smoke_qt` passed 1/1 and `smoke_core` reported OK. The UI test covers log-search settings round-trip, isolation on other pages, restoration when returning to Logs, and restoration after creating a new window.
2. Installed 0.6.13 into an isolated application directory; installed EXE ProductVersion was 0.6.13.
3. Installed 0.6.14 over 0.6.13 at the same path. HKCU uninstall `DisplayVersion`, installed EXE ProductVersion and FileVersion all report 0.6.14.
4. With `PATH=C:\Windows\System32;C:\Windows` and Qt plugin environment overrides cleared, the installed real-window `--smoke-test` exited 0, created `smoke\installed-window.png` (40,910 bytes), and produced empty stderr.
5. The uninstaller exited 0 and removed the application EXE and HKCU uninstall entry. A separate user-data marker outside the application directory remained unchanged.

## Scope

Qt migration checkpoint 168 persists the application-log search query in the isolated Qt workspace. See `data/meta/patch-notes/0.6.14.md`.
