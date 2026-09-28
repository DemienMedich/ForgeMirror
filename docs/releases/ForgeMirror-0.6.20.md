# ForgeMirror 0.6.20 Qt installer verification

- Canonical version: root `VERSION` = `0.6.20`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.20.exe`.
- Size: `11,513,693` bytes.
- SHA-256: `6EA692D29D4EA42B61CED0B0EA59241898DB9DE73A0266279AA374CFC77028A2`.
- Setup ProductVersion and packaged EXE ProductVersion/FileVersion: `0.6.20`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; current-user installation (`PrivilegesRequired=lowest`).
- Payload: 28 files.

## Verification

- `build-qt.ps1 -Package`: passed; `smoke_qt` 1/1 and `smoke_core: OK`.
- Module parity regression test disabled all supported modules and verified navigation visibility, achievement action visibility, and fallback from a stored Cloud page.
- Administrator-auth regression tests repeatedly toggled the persistent-session preference while checking that the password remained unchanged; they verified restore in fresh `QtWindow` instances (same-process restart simulation), logout, nonpersistent login, and password rotation.
- Isolated lifecycle: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage174-b1e7c552b7c343cfbede31ff2b034bfe`. Installed 0.6.19 and updated the same isolated directory to 0.6.20. Verified installed EXE ProductVersion/FileVersion `0.6.20`. The installed executable launched with `PATH` restricted to Windows directories and Qt plugin environment variables cleared (exit 0; screenshot `installed-window.png`, 45,481 bytes). Uninstall removed the application directory and uninstall registry entry; a separate user-data marker survived.
- Inno Setup 6.7.3 compiled the installer successfully. It emitted the existing non-blocking `[UninstallRun]` warning about `RunOnceId`.

## Scope

Stage 174 makes Qt honor the stable ImGui module-disable switches for Tasks, Pipeline, Achievements, Shortcuts, Pomodoro, Cloud, 3D and Professions. Disabled pages and profile actions are hidden, a saved selection of a disabled page falls back to Profile, and disabling Cloud prevents manual and automatic cloud actions.
