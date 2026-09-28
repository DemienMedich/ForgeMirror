# ForgeMirror Qt 0.6.70 release verification

- Canonical version: root `VERSION` = `0.6.70`.
- Scope: stage 225 fixes one functional migration mismatch. Legacy F6 opens Logs; Qt had routed it to the added Audit page. The key now opens Logs, its navigation tooltip and shortcut-help entry agree, and Audit has no F6 hint.
- Windows installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.70.exe`; 11,572,625 bytes; FileVersion/ProductVersion `0.6.70`; SHA-256 `1A5CFF316722052B9DF9843CB1D0F8AF9DBC5D277E9E3D8E498E0605580CE808`.
- Packaged Qt executable: `Z:\CPP\ForgeMirror\package-qt-0.6.70\ForgeMirrorQt.exe`; 3,412,992 bytes; FileVersion/ProductVersion `0.6.70`; SHA-256 `D4D40CC93E27452D6F69645726D43D8BFAA1CCA602280B2996E07D27506668C0`.
- ImGui compatibility executable, rebuilt from the same canonical version: `Z:\CPP\ForgeMirror\build-gui\Release\ForgeMirrorGui.exe`; 2,978,816 bytes; FileVersion/ProductVersion `0.6.70`; SHA-256 `724E0A0226D73E2FC3FD01358B96986E4F3BF40CA18367FD7276C4DF94E4B610`.
- The release installer uses the stable production AppId `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}` and remains a per-user installation. The lifecycle test used a separate temporary AppId `{26F34FDC-C40E-469F-B7F6-DF6E94BA389C}` and an installation directory under `build-qt`; the production installation and AppId were not used. The package contains no developer `data` directory.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.70` passed. CTest `smoke_qt` passed 1/1 in 26.28 seconds; `smoke_core` reported `OK`. The Qt test checks the shortcut target page, F6 tooltip placement, and shortcut-help destination.
- Packaged `--version` reported `ForgeMirrorQt 0.6.70`. With `PATH` restricted to Windows system directories and Qt plugin overrides removed, packaged `--smoke-test` exited 0 and saved `Z:\CPP\ForgeMirror\build-qt\runtime-0.6.70.png` (50,449 bytes).
- The isolated lifecycle compiled the same installer script for 0.6.69 and 0.6.70 using the temporary AppId and directory. Version 0.6.69 installed with matching uninstall `DisplayVersion` and executable ProductVersion, then launched with minimal `PATH` and Qt environment overrides cleared (exit 0; screenshot 52,319 bytes). Updating in place to 0.6.70 changed both registered and executable versions to `0.6.70`; the 0.6.70 installed EXE also passed the real-window smoke with minimal `PATH` (exit 0; screenshot 52,423 bytes).
- Silent uninstall exited 0 and removed the isolated EXE and uninstall registration. The separate user-data marker remained SHA-256 `F85DD9FFCA9F9EF649BA060003110BECDBC923CBD193CCE1C272067CC8D6FC83`; the workspace marker remained `ED56255B852E200D900CAA12FD67FD92DCF4D1D0182481A36407832C86D6F857`.
- Installer compilation used Inno Setup 6.7.3. Production AppId/install were not touched; `develop` and `origin/develop` remain unchanged. `git diff --check` passed before commit.

Functional migration is still estimated at about 90%; that is an expert estimate, not a code or test percentage. All 18 legacy workspace tabs have Qt surfaces, and the action-level comparison is still underway. Remaining follow-up includes hands-on NVDA/JAWS testing, broader core event coverage, and coordination with older or external writers that bypass the shared lock.
