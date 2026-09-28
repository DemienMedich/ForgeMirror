# ForgeMirror Qt 0.6.62 release verification

- Canonical version: root `VERSION` = `0.6.62`.
- Scope: serialize Qt and shared-core cloud mutations against cooperating workspace writers; exclude the live lock file from Qt pull staging and backup copies.
- Release installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.62.exe`; 11,565,598 bytes; SHA-256 `2F8D689E4DCFAB2739408E87A5F810C5750C2BACF84F0719CB210982C4604665`.
- Packaged executable: `Z:\CPP\ForgeMirror\package-qt-0.6.62\ForgeMirrorQt.exe`; 3,381,248 bytes; FileVersion/ProductVersion `0.6.62`; SHA-256 `F455F6CCF66DE401F493355335F6906A7464BE52AD5B337BB49C87D5B3A91A60`.
- Installer FileVersion/ProductVersion: `0.6.62`; compiled with Inno Setup 6.7.3. Production AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}` and installation remains per-user.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.62` completed. `smoke_qt` passed 1/1 (26.29 sec); `smoke_core: OK`.
- Lock regressions hold the OS workspace lock through a second handle and verify that Qt pull/push, preview, recovery, single-file/catalog conflict transfers, storage conflict resolution, release download, core sync/config/manifest changes, and backup restoration refuse before partial writes. Existing transaction failure-injection, recovery, and retry tests pass.
- Packaged real-window smoke ran with `PATH=C:\Windows\System32;C:\Windows` and Qt environment overrides cleared. Exit code was 0; stderr was empty. Screenshot: `Z:\CPP\ForgeMirror\build-qt\package-smoke-final-0.6.62.png` (51,542 bytes).
- `installer\build-qt-installer.ps1 -PackageDirectory .\package-qt-0.6.62` built the actual per-user installer at the path above. Setup FileVersion/ProductVersion both match `0.6.62`.
- Isolated lifecycle directory: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.62-77590412e294418cb290e1a3e3638c74`; temporary test AppId `{84641C27-84D5-475F-8DD7-04530F7F8D6C}`. The `0.6.61` installer installed, then `0.6.62` updated it in place. At both versions installer exit was 0, EXE FileVersion/ProductVersion and HKCU uninstall DisplayVersion matched, and installed-window smoke exited 0 without Qt on `PATH` (screenshots 52,883 and 52,815 bytes). Silent uninstall exited 0 and removed the EXE and test registry entry. Workspace and app-data marker hashes were unchanged through install, update, and uninstall.
- The stable ImGui baseline and `develop` were not modified. The isolated lifecycle test substituted a temporary AppId and did not alter the production installation or user data.

The installer is the release deliverable; the package directory is retained for QA only. No workspace, profile, cloud configuration, or developer-local storage is included in the installer.
