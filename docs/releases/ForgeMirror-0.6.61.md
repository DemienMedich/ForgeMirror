# ForgeMirror Qt 0.6.61 release verification

- Canonical version: root `VERSION` = 0.6.61.
- Scope: allocate a profile ID under the shared workspace-write lock and refresh stale `FileStorage` ID caches before saving.
- Production installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.61.exe`; 11,565,012 bytes; SHA-256 `1BB9986F922D10427862B61F78D6F1DB5B6B4A5F02ECA764D4DEA7D67CE69584`.
- Packaged executable: `Z:\CPP\ForgeMirror\package-qt-0.6.61\ForgeMirrorQt.exe`; 3,378,176 bytes; FileVersion/ProductVersion 0.6.61; SHA-256 `F738F23D7B1B6EE25F7E38FBCA495EB9569106B075A1F9C45E1DC9D439614F6F`.
- Installer FileVersion/ProductVersion: 0.6.61; compiled with Inno Setup 6.7.3. Production AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}` and installation remains per-user.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.61` completed. `smoke_qt` passed 1/1 (23.17 sec); `smoke_core: OK`; `git diff --check` passed.
- The new `smoke_core` regression constructs two `FileStorage` clients before either creates a profile. The pre-fix implementation failed because the second stale ID replaced the first profile. After the fix, both IDs and names remain present.
- Packaged startup with `PATH=C:\Windows\System32;C:\Windows` and Qt environment overrides cleared exited 0 and saved `build-qt\package-smoke-0.6.61-2b6e90482bf14bf08c3214ccc44bc991\window.png` (52,362 bytes).
- `installer\build-qt-installer.ps1 -PackageDirectory .\package-qt-0.6.61` built the actual current-user installer at the path above. Setup FileVersion/ProductVersion both match 0.6.61.
- Isolated lifecycle directory: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.61-9ecb254e18924042b904ba73ba46d395`; temporary test AppId `{A142EE21-0719-49DD-A8B8-BC1346FB35A1}`. The 0.6.60 installer installed, then 0.6.61 updated in place. At both versions, installer exit was 0, EXE FileVersion/ProductVersion and HKCU uninstall DisplayVersion matched, and the installed window smoke exited 0 without Qt on `PATH` (screenshots 52,784 and 52,767 bytes). Silent uninstall exited 0 and removed the EXE and test registry entry. Separate workspace and app-data marker hashes were unchanged through install, update, and uninstall.
- `origin/develop` remains `7306152c603ff8007200f64e63c4188510d55588`; the stable ImGui baseline was not modified. The production user installation was not part of the isolated lifecycle test.

The installer is the release deliverable; the package directory is retained for QA only. No user data or developer-local storage is included in the installer.
