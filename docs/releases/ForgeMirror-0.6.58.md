# ForgeMirror Qt 0.6.58 release verification

- Canonical version: root `VERSION` = `0.6.58`.
- Scope: replace task/profile-specific sidecars with one reentrant workspace write lock and hold it through current Qt recovery-journal transactions. Older binaries and tools that ignore the new lock remain uncoordinated.
- Production installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.58.exe`; 11,563,921 bytes; SHA-256 `7D467CE0E16E4C3F27F67C05FE4B264BF60C5E54FA3361A58EA4ACF9C1C842C9`.
- Packaged executable: `Z:\CPP\ForgeMirror\package-qt-0.6.58\ForgeMirrorQt.exe`; 3,376,128 bytes; FileVersion/ProductVersion `0.6.58`; SHA-256 `761D1D7715AFFEC0C64AA5FD7CC581392674B4AA9916E4B10A0C832CD402E0E9`.
- Installer FileVersion/ProductVersion: `0.6.58`; compiled with Inno Setup 6.7.3. Production AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; installation remains per-user.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.58` completed. `smoke_qt` passed (25.45 sec), `smoke_core: OK`; a separate `build-gui` compile of the stable ImGui `ForgeMirror` target also succeeded.
- The packaged executable opened its real Qt window with `PATH=C:\Windows\System32;C:\Windows` and Qt environment overrides cleared. Smoke exit was 0 and `build-qt\package-smoke-0.6.58\window.png` was 51,474 bytes.
- The production installer matched the packaged EXE version. Isolated lifecycle directory: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.58-dec4eb09ca40481a8c60bcde7df1b602`, using test-only AppId `{5B30E7B4-1DE1-4DB1-AF4E-D279D1C44727}`. The 0.6.57 test installer installed successfully, then updated in place to 0.6.58; installed EXE ProductVersion/FileVersion and HKCU uninstall `DisplayVersion` all reported 0.6.58.
- The isolated installed executable opened its real window with Qt removed from `PATH` (exit 0; screenshot `installed-window.png`, 52,582 bytes). Silent uninstall exited 0, removed the EXE and test uninstall record, and preserved marker files in both the workspace and separate local application-data directory.
- The production current-user install updated from 0.6.57 to 0.6.58 at `C:\Users\mrdem\AppData\Local\Programs\ForgeMirror\ForgeMirrorQt.exe` (installer exit 0). The installed EXE metadata and HKCU uninstall record both report 0.6.58. Before/after inventories each contained 46 workspace files; the only changed workspace file was the existing `meta/qt-reminder-state.json` watermark, advanced by the running 0.6.57 client's normal minute timer while the update was being prepared. The other 45 workspace files remained byte-identical; no files were removed or added. The nested app-local-data inventory reported the same one-file change because it contains the workspace.
- The production Qt window was reopened and showed title `ForgeMirror · Qt migration · 0.6.58` (PID 19996 at verification time). `develop` remains at `7306152c603ff8007200f64e63c4188510d55588`; the user's `AgentsSkills/CONTINUITY.md` changes were not included.

## Migration status

Stage 213 adds shared locking for cooperating storage writers and journaled transactions. Functional Qt migration remains an expert estimate of about 90%, not a code/test percentage; remaining parity and accessibility gaps are listed in `qt/README.md`.
