# ForgeMirror 0.6.36 Qt installer verification

- Canonical version: root `VERSION` = `0.6.36`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.36.exe`.
- Size: `11,539,390` bytes.
- SHA-256: `08F0D03CE82C791AE15491CFE668734D151EEA42F4504CAA5A7C2F6C451AE717`.
- Setup ProductVersion and packaged EXE ProductVersion/FileVersion: `0.6.36`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; current-user installation (`PrivilegesRequired=lowest`).
- Payload: 28 files.

## Verification

- `build-qt.ps1 -Package`: passed; `smoke_qt` 1/1 and `smoke_core: OK`.
- Cloud pull journals now include before/after existence and SHA-256 state. Recovery validates the full target set before changing files; a third external state leaves every target and the recovery journal untouched. After resolving the conflicting target, recovery retries successfully. Legacy version-1 journals fail closed when a target no longer matches its pre-image.
- `--version` and `--help` now exit before loading the workspace or creating the main window. Separate-process tests isolate `LOCALAPPDATA`/`APPDATA`, check output and exit code, and verify no default workspace was created.
- Inno Setup 6.7.3 compiled successfully. It emitted the existing non-blocking `[UninstallRun]` warning about `RunOnceId`.
- Isolated lifecycle directory: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage190-final-2a64721093bc49c8b5533bc9a29b6443`. Installed 0.6.35 and updated in place to 0.6.36 under the stable AppId. EXE ProductVersion/FileVersion and HKCU uninstall DisplayVersion matched after both steps; install location stayed isolated.
- Installed 0.6.36 `--version` returned `ForgeMirrorQt 0.6.36`; `--help` listed the options. Both exited successfully without opening the main UI.
- Installed smoke launch used a disposable workspace, Windows Qt platform plugin, `PATH=C:\Windows\System32;C:\Windows`, and cleared Qt plugin overrides. It exited 0, produced a 54,729-byte screenshot, and wrote no stderr.
- Silent uninstall exited 0, removed the isolated application and uninstall entry, and preserved the separate user-data marker. The stable ImGui uninstall entry remained at version 0.5.27 and `Z:\Soft\ForgeMirror\`.
- The user's separate installed Qt executable remained at 0.6.18; it was not upgraded, replaced, or terminated. The stable `develop` branch remains unchanged.

## Scope

Stage 190 protects recovery of interrupted cloud pulls from overwriting changed local targets. Broader non-cloud multi-file transactions and other parity items remain unfinished; this is not a claim that the full Qt migration is complete.
