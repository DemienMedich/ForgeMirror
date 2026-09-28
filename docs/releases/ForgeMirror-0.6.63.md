# ForgeMirror Qt 0.6.63 release verification

- Canonical version: root `VERSION` = `0.6.63`.
- Scope: fix profile analytics layout so the radar-axis toolbar stays above custom-painted charts and does not overlap the radar heading; reserve space from live Qt layout metrics rather than a fixed pixel guess.
- Release installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.63.exe`; 11,565,465 bytes; SHA-256 `CFA9204FF6F1586A5EB7B1D10DB30618EDC82B1F4034B106EB3A55E123EAB7C9`.
- Packaged executable: `Z:\CPP\ForgeMirror\package-qt-0.6.63\ForgeMirrorQt.exe`; 3,381,248 bytes; FileVersion/ProductVersion `0.6.63`; SHA-256 `E86830FB73CB3219099E02E316F602AB181261721A2B87D32A2F5F4B5268997E`.
- Installer FileVersion/ProductVersion: `0.6.63`; compiled with Inno Setup 6.7.3. Production AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}` and installation remains per-user.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.63` completed. `smoke_qt` passed 1/1 (23.08 sec); `smoke_core: OK`. The profile analytics regression checks that the axis control is top-aligned and ends before the custom-painted chart area.
- Packaged real-window smoke ran with `PATH=C:\Windows\System32;C:\Windows` and Qt environment overrides cleared. Exit code was 0; screenshot is `Z:\CPP\ForgeMirror\build-qt\runtime-verify-0.6.63\window.png` (51,065 bytes). Visual check confirms the axis control sits above the charts and does not cover the radar heading.
- `installer\build-qt-installer.ps1 -PackageDirectory .\package-qt-0.6.63` built the actual per-user installer at the path above. Setup FileVersion/ProductVersion both match `0.6.63`.
- Isolated lifecycle directory: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.63-data-check-9ca517a69c4d4c19b5361e4d79d77b27`; temporary test AppId `{646F8A43-3482-4D14-9C5C-5A618BB0A05E}`. Temporary-AppId installers installed `0.6.62`, then updated it in place to `0.6.63`; both setup exits were 0, installed executable versions matched each release, installed-window smoke exited 0 without Qt on `PATH` (screenshots 52,648 and 52,436 bytes), and the HKCU uninstall `DisplayVersion` matched `0.6.63`. Silent uninstall exited 0, removed the executable and test registry entry, and preserved both workspace and local-app-data marker hashes.
- The stable ImGui baseline and `develop` were not modified. The isolated lifecycle test used a temporary AppId and did not alter the production installation or user data.

The installer is the release deliverable; the package directory is retained for QA only. No workspace, profile, cloud configuration, or developer-local storage is included in the installer.
