# ForgeMirror Qt 0.6.64 release verification

- Canonical version: root `VERSION` = `0.6.64`.
- Scope: route the remaining ImGui compatibility client's cloud storage-conflict action through the shared workspace-write lock, validated staging, local pre-image backup, and atomic replacement; keep the preserved baseline and `develop` unchanged.
- Release installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.64.exe`; 11,564,385 bytes; SHA-256 `64DBC1A9FFAAD9C9C1597CB95BB3E7B460D20CF1CC6C5A610DAA04EA740FD6A6`.
- Packaged executable: `Z:\CPP\ForgeMirror\package-qt-0.6.64\ForgeMirrorQt.exe`; 3,380,736 bytes; FileVersion/ProductVersion `0.6.64`; SHA-256 `011662B1ED6170A6130F1F7F46EF7AA8AC4062FB695E1CC490965858AB8AF2A3`.
- Installer FileVersion/ProductVersion: `0.6.64`; compiled with Inno Setup 6.7.3. Production AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}` and installation remains per-user.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.64` completed. `smoke_qt` passed 1/1 (29.16 sec); `smoke_core: OK`. The new core regression rejects invalid and out-of-directory conflict snapshots, refuses while the workspace lock is held without creating a backup or changing the target, and verifies a successful atomic replacement with exact local pre-image preservation.
- The ImGui compatibility target `ForgeMirrorGui` rebuilt successfully with ProductVersion/FileVersion `0.6.64`, so the legacy conflict action compiles against the shared service.
- Packaged real-window smoke ran with `PATH=C:\Windows\System32;C:\Windows` and Qt environment overrides cleared, using a disposable `--storage-dir`. Exit code was 0; screenshot: `Z:\CPP\ForgeMirror\build-qt\runtime-verify-result-0.6.64-e465814fe5c648ad8e9358fda5c642dc\window.png` (52,615 bytes).
- `installer\build-qt-installer.ps1 -PackageDirectory .\package-qt-0.6.64` built the actual per-user installer at the path above. Setup FileVersion/ProductVersion both match `0.6.64`.
- Isolated lifecycle directory: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.64-6e14e68f2731452ebd4079fc9a6b4acc`; temporary test AppId `{5F7C88C4-B57E-4E2A-9E83-FC91A5D44628}` and test-only install directory. A test installer for `0.6.63` installed, then `0.6.64` updated it in place; both installer exits were 0, installed EXE versions matched, and both installed-window smokes exited 0 without Qt on `PATH` (screenshots 52,394 and 52,275 bytes). The HKCU uninstall `DisplayVersion` matched `0.6.64`. Silent uninstall exited 0, removed the executable and test registry entry, and preserved the external workspace marker SHA-256 `282442D1F6C53636FB6DE32435CB64CCE1D5F8E0EBF61BC943D00D8FF4162A28`.
- Lifecycle testing used a temporary AppId and dedicated install directory; it did not alter the production installation or `develop`. The saved ImGui baseline branch remains unchanged.

The installer is the release deliverable; the package directory is retained for QA only. No workspace, profile, cloud configuration, or developer-local storage is included in the installer.
