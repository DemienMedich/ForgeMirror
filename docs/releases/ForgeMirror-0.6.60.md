# ForgeMirror Qt 0.6.60 release verification

- Canonical version: root VERSION = 0.6.60.
- Scope: record privacy-safe committed, failed/rolled-back, or recovery-pending outcomes for permanent deletion of an empty archived profile.
- Production installer: Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.60.exe; 11,565,249 bytes; SHA-256 9CFD8EE0D854EF6AC46431921B0801EC9F5ED41F0179AF3D53852B50D314A5CB.
- Packaged executable: Z:\CPP\ForgeMirror\package-qt-0.6.60\ForgeMirrorQt.exe; 3,378,176 bytes; FileVersion/ProductVersion 0.6.60; SHA-256 130D04E6475B37ECE367F1113770EF5CDBD2A2C1215926B22456C601BF36763B.
- Installer FileVersion/ProductVersion: 0.6.60; compiled with Inno Setup 6.7.3. Production AppId remains {8B99E76B-4510-49D8-AE45-9DDF85EA21DC} and installation remains per-user.

## Verification

- build-qt.ps1 -Package -PackageDirectory package-qt-0.6.60 completed. smoke_qt passed 1/1 (24.52 sec), smoke_core: OK. After adding the throwing-event-observer regression, a final build-qt.ps1 rerun also passed smoke_qt 1/1 (25.06 sec) and smoke_core: OK; git diff --check passed.
- The packaged executable reported ForgeMirrorQt 0.6.60; FileVersion and ProductVersion both matched. With PATH=C:\Windows\System32;C:\Windows and Qt environment overrides cleared, its real-window smoke test exited 0 and saved build-qt\package-smoke-0.6.60\window.png (51,341 bytes).
- installer\build-qt-installer.ps1 -PackageDirectory .\package-qt-0.6.60 built the actual per-user installer at the path above. Setup FileVersion and ProductVersion both report 0.6.60.
- Isolated lifecycle directory: Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.60-ae7e876ac8824ae5859c984289395610; temporary test AppId {7308A475-4FC3-4B62-8A16-458F7FD7937E}. The 0.6.59 test installer installed, then 0.6.60 updated in place. At both versions, installer exit was 0, EXE FileVersion/ProductVersion and HKCU uninstall DisplayVersion matched, and the installed window smoke exited 0 without Qt on PATH (screenshots 52,093 and 52,148 bytes). Silent uninstall exited 0 and removed the EXE and test registry entry. Separate workspace and app-data marker hashes were unchanged through installation, update, and uninstall.
- The existing production installation was not changed during this isolated test because its 0.6.59 process (PID 16820) was still running. Its installed EXE reports ForgeMirrorQt 0.6.59; the 0.6.60 installer is ready for a later update.
- origin/develop remains 7306152c603ff8007200f64e63c4188510d55588; the stable ImGui source was not changed by this release.

## Migration status

Stage 215 closes the profile-deletion outcome logging mismatch. Functional Qt migration remains an expert estimate of about 90%, not a code/test percentage. Manual NVDA/JAWS interaction certification, non-UI core telemetry, and coordination with older/external writers that bypass the shared workspace lock remain open; see qt/README.md.
