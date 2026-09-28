# ForgeMirror 0.6.26 Qt installer verification

- Canonical version: root `VERSION` = `0.6.26`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.26.exe`.
- Size: `11,520,338` bytes.
- SHA-256: `88A761711F9C302321CD997B6273265C7B7AF1A9EBFC0ED67EA8CC5459D93504`.
- Setup ProductVersion and packaged EXE ProductVersion/FileVersion: `0.6.26`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; current-user installation (`PrivilegesRequired=lowest`).
- Payload: 28 files.

## Verification

- `build-qt.ps1 -Package`: passed; `smoke_qt` 1/1 and `smoke_core: OK`.
- `smoke_qt` verifies persistent launcher accessibility, process states (running/stopped/unknown), manage-shortcuts navigation, and module-disable visibility.
- Isolated lifecycle directory: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage180-final-647b3053a4584a79bbef2811fb2dd4c1`. Installed 0.6.25, updated in-place to 0.6.26 under the stable AppId, and verified EXE ProductVersion/FileVersion plus HKCU uninstall DisplayVersion.
- Installed 0.6.26 launched with `PATH=C:\Windows\System32;C:\Windows`, Qt plugin environment overrides cleared, disposable `--storage-dir`, `--screenshot` and `--smoke-test`; exit code 0, screenshot 46,287 bytes.
- Silent uninstall exited 0 and removed the application directory. A separate user-data marker remained intact.
- Inno Setup 6.7.3 compiled successfully. It emitted the existing non-blocking `[UninstallRun]` warning about `RunOnceId`.

## Scope

Stage 180 ports the legacy quick-launch sidebar behavior into a persistent Qt header menu. It reuses the local shortcut list, provides one-shot process status when opened, and retains the established Qt palette. No production workspace was used for lifecycle testing.

