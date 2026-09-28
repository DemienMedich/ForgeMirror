# ForgeMirror Qt 0.6.59 release verification

- Canonical version: root `VERSION` = `0.6.59`.
- Scope: record profile creation in the existing profile audit, matching the stable ImGui workflow while excluding the generated password.
- Production installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.59.exe`; 11,563,176 bytes; SHA-256 `0FAC25F3D8A175599A0CF4B65910FB5C9936FCEE671FDF65F54111E4FE682F6B`.
- Packaged executable: `Z:\CPP\ForgeMirror\package-qt-0.6.59\ForgeMirrorQt.exe`; 3,377,664 bytes; FileVersion/ProductVersion `0.6.59`; SHA-256 `5D4438D24F6217078DE96E5216C6E1B8DDF2BC2D492F1D85EB2BA1BD25E2277E`.
- Installer FileVersion/ProductVersion: `0.6.59`; compiled with Inno Setup 6.7.3. Production AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}` and installation remains per-user.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.59` completed. `smoke_qt` passed 1/1 (22.77 sec), `smoke_core: OK`, and `cmake --build build-gui --config Release --target ForgeMirror` built the preserved ImGui client. `git diff --check` passed.
- The packaged executable reported `ForgeMirrorQt 0.6.59`; it opened its real window with `PATH=C:\Windows\System32;C:\Windows` and Qt environment overrides cleared. `--smoke-test` exited 0 and saved `build-qt\package-smoke-0.6.59\window.png` (51,532 bytes; SHA-256 `B2A6175FFD3EA4AFA77513593AF0BC53F6D4E0926D906F87EBEE88F8E782D221`).
- Isolated lifecycle root: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.59-4a5fab93d0ce4babb94adbbd1133b06e`; temporary test AppId `{33067B16-4C89-4359-AD5A-7451DCAD7932}`. The .58 test installer installed, then the .59 installer updated the same directory. EXE ProductVersion/FileVersion and HKCU uninstall DisplayVersion matched each version. Both installed applications opened real windows without Qt in `PATH` (screenshots 52,206 and 52,289 bytes). Silent uninstall exited 0, removed the EXE and uninstall entry, and preserved marker files in both the separate workspace and separate user-data directory.
- The production current-user installer updated .58 to .59 at `C:\Users\mrdem\AppData\Local\Programs\ForgeMirror\ForgeMirrorQt.exe` (exit 0). Installed EXE metadata and HKCU uninstall DisplayVersion report .59. The production data-tree snapshot contained 47 files before and after; it excludes the process-owned `workspace.qt.lock`. No files were added or removed. The only changed file was `workspace\meta\qt-reminder-state.json` while the old process closed; the before/after tree hashes were `7FC9D91546EA3A80B2B122877A9194397BFA59B1EBBBC68E0C23FDF50C55841C` and `B4D27238C62A5097DB1A2F40D3FF2727DE7BC8A9A2074216D22FDF4B5FAB3619`.
- The production application was reopened as PID 16820 with title `ForgeMirror · Qt migration · 0.6.59`. The preserved ImGui entry point `gui/GuiApp.cpp` matches `origin/develop`; `develop` remains at `7306152c603ff8007200f64e63c4188510d55588`.

## Migration status

Stage 214 closes the profile-creation history mismatch. Functional Qt migration remains an expert estimate of about 90%; see the remaining parity and accessibility gaps in `qt/README.md`.
