# ForgeMirror 0.6.23 Qt installer verification

- Canonical version: root `VERSION` = `0.6.23`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.23.exe`.
- Size: `11,518,896` bytes.
- SHA-256: `C48ED656488EF0A0C1AE3C454E406865F988D478CC3F5FC273A5441AA182AA9F`.
- Setup ProductVersion and packaged EXE ProductVersion/FileVersion: `0.6.23`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; current-user installation (`PrivilegesRequired=lowest`).
- Payload: 28 files.

## Verification

- `build-qt.ps1 -Package`: passed; `smoke_qt` 1/1 and `smoke_core: OK`.
- Ctrl+F10 regression coverage verifies shortcut registration and help text, canonical display/Pomodoro/3D defaults, atomic `ui.ini` reset, retained profile identity and trust/recent entries, unchanged administrator credential/session preference, unchanged cloud section and custom layout presets, and retained model, music and background files.
- Isolated lifecycle: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage177-final-e71da497ea484256bd4d136c3c86ffba`. Installed 0.6.22, then updated in the same directory to 0.6.23 using the stable AppId. Verified installed EXE ProductVersion/FileVersion, uninstall registry DisplayVersion and InstallLocation.
- The installed 0.6.23 EXE opened the real Qt window with `PATH=C:\Windows\System32;C:\Windows` and Qt plugin environment overrides cleared; `--smoke-test` exited 0, stderr was empty, and `installed-window.png` was 45,721 bytes.
- Silent uninstall exited 0, removed the application directory and uninstall registry entry, and preserved a separate user-data marker. No pre-existing ForgeMirror uninstall record or opt-in deadline task was present before the isolated test.
- Inno Setup 6.7.3 compiled the installer successfully. It emitted the existing non-blocking `[UninstallRun]` warning about `RunOnceId`.

## Scope

Stage 177 ports Ctrl+F10's interface reset. Qt resets display, Pomodoro and model-view settings as one atomic update, preserves profile trust and unrelated user data, and restarts into the same workspace. The existing installed application and production workspace were not used by the lifecycle test.
