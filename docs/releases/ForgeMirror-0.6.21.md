# ForgeMirror 0.6.21 Qt installer verification

- Canonical version: root `VERSION` = `0.6.21`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.21.exe`.
- Size: `11,511,552` bytes.
- SHA-256: `EC9111B14EBC6EFA3081390F8B84A2F9FF05FE349A1C19104F1436F7B35819B6`.
- Setup ProductVersion and packaged EXE ProductVersion/FileVersion: `0.6.21`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; current-user installation (`PrivilegesRequired=lowest`).
- Payload: 28 files.

## Verification

- `build-qt.ps1 -Package`: passed; `smoke_qt` 1/1 and `smoke_core: OK`.
- Authentication window tests verify the login action's text and tooltip in logged-out, logged-in, remembered-session-restored and post-logout states. The persistent checkbox remains independently tested for repeated saves and password preservation.
- Isolated lifecycle: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage175-6d25f10acbad40a28b016fa85e3935ef`. Installed 0.6.20 and updated the same isolated directory to 0.6.21. Verified installed EXE ProductVersion/FileVersion and uninstall registry version `0.6.21`, stable AppId and exact install location. The installed executable launched with `PATH` restricted to Windows directories and Qt plugin environment variables cleared (exit 0; screenshot `installed-window.png`, 45,625 bytes). Uninstall removed the application directory and uninstall registry entry; a separate user-data marker survived.
- Inno Setup 6.7.3 compiled the installer successfully. It emitted the existing non-blocking `[UninstallRun]` warning about `RunOnceId`.

## Scope

Stage 175 clarifies the stateful administrator menu action: logged-out users see **Войти как администратор**, and logged-in users see **Выйти из режима администратора** with a tooltip explaining that persistent restoration is disabled on logout. Password verification and preference storage behavior are unchanged.
