# ForgeMirror 0.6.37 Qt installer verification

- Canonical version: root `VERSION` = `0.6.37`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.37.exe`.
- Size: `11,539,618` bytes.
- SHA-256: `851556FCD5CA22A4F88059C492B4AA77487FFDF80872DB74F7FE9813CED8AD7B`.
- Setup ProductVersion and packaged EXE ProductVersion/FileVersion: `0.6.37`.
- Production AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; current-user installation (`PrivilegesRequired=lowest`).
- Payload: 28 files.

## Verification

- `build-qt.ps1 -Package` passed: `smoke_qt` 1/1 and `smoke_core: OK`.
- Regression coverage restores a remembered administrator session with Pomodoro selected, checks the admin controls are visible, and runs the event loop for 3.2 seconds. This reproduces the previously crashing saved-page startup path.
- Packaged Windows-platform launch used a disposable workspace with `stayLoggedIn=1`, `lastPage=8` (Pomodoro), and fullscreen enabled. It survived four seconds and logged `Administrator session restored`.
- Lifecycle test used disposable installer builds with a separate test-only AppId so the real production registration was not disturbed. It installed 0.6.36, updated in place to 0.6.37 under the same test AppId and install path, verified EXE ProductVersion and HKCU DisplayVersion, launched with Qt removed from `PATH`, then silently uninstalled. The external user-data marker remained; the stable ImGui registration remained at 0.5.27.
- Inno Setup 6.7.3 compiled the production installer successfully. It emitted the existing non-blocking `[UninstallRun]` warning about `RunOnceId`.
- Production install at `%LOCALAPPDATA%\Programs\ForgeMirror` updated in place from 0.6.36 to 0.6.37; installer exit code was 0. EXE ProductVersion and HKCU DisplayVersion both report 0.6.37. `meta/admin.ini` SHA-256 stayed `C73ACA6E0C6FC84D362B5D63E8751AC29C69FD020EE3D1983270018EABB99807` and `stayLoggedIn=1` remained enabled.
- The installed production executable was opened normally from its install directory. After five seconds it remained alive as PID 8320 with title `ForgeMirror · Qt migration · 0.6.37`; the Qt log recorded one additional `Administrator session restored` event and no new rejected-login events.
- The desktop shortcut `ForgeMirror Qt.lnk` targets the Qt installation. The existing `Программы\ForgeMirror.lnk` still targets the separate stable ImGui client; the stable uninstall registration remains 0.5.27.
- `develop` and `origin/develop` were not changed.

## Scope

Stage 191 defers restoration of a saved Pomodoro page until after the main window is shown, preventing the Windows Qt Widgets access violation observed on startup. The remembered administrator preference and saved page are preserved. Other Qt migration parity gaps remain open.
