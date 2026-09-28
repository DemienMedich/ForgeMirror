# ForgeMirror 0.6.22 Qt installer verification

- Canonical version: root `VERSION` = `0.6.22`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.22.exe`.
- Size: `11,512,483` bytes.
- SHA-256: `7A47BA6F0EC376BD3B8FFA830B95888F6FA15C15B1E7811694C2A7079B3CD663`.
- Setup ProductVersion and packaged EXE ProductVersion/FileVersion: `0.6.22`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; current-user installation (`PrivilegesRequired=lowest`).
- Payload: 28 files.

## Verification

- `build-qt.ps1 -Package`: passed; `smoke_qt` 1/1 and `smoke_core: OK`.
- Window-decoration regression test exercised F10 both directions, settings persistence into a fresh Qt window, native frameless flag and drag-handle visibility. Shortcut help verifies the F10 row and binding.
- Isolated lifecycle: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage176-f81353cc0e854d869c2cdc367cd78fb1`. Installed 0.6.21 and updated the same isolated directory to 0.6.22. Verified installed EXE ProductVersion/FileVersion and uninstall registry version `0.6.22`, stable AppId and exact install location. The installed executable launched with `PATH` restricted to Windows directories and Qt plugin environment variables cleared (exit 0; screenshot `installed-window.png`, 45,674 bytes). Uninstall removed the application directory and uninstall registry entry; a separate user-data marker survived.
- Inno Setup 6.7.3 compiled the installer successfully. It emitted the existing non-blocking `[UninstallRun]` warning about `RunOnceId`.

## Scope

Stage 176 restores the stable interface's F10 window-decoration toggle. The Qt client saves the setting locally, maintains the native drag handle when frameless, and documents the shortcut in keyboard help. Ctrl+F10's broader UI reset behavior is not part of this stage.
