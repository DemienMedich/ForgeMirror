# ForgeMirror 0.6.24 Qt installer verification

- Canonical version: root `VERSION` = `0.6.24`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.24.exe`.
- Size: `11518729` bytes.
- SHA-256: `927176B305800C9D5A2BD4E41B9614761E43F5E7DFC9F06E0D3954AE18253E31`.
- Setup ProductVersion and packaged EXE ProductVersion/FileVersion: `0.6.24`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; current-user installation (`PrivilegesRequired=lowest`).
- Payload: 28 files.

## Verification

- `build-qt.ps1 -Package`: passed; `smoke_qt` 1/1 and `smoke_core: OK`.
- Project filter regression covers import from legacy `[projects] filter`, persistence across navigation and window reconstruction, matching name/description, filter reset that preserves sort, and accessible action naming.
- Isolated lifecycle directory: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage178-362853a425db40db93023333738b6900`. Installed 0.6.23, then updated in-place to 0.6.24 with the same AppId. Verified installed EXE ProductVersion/FileVersion and HKCU uninstall DisplayVersion were all `0.6.24`.
- Installed 0.6.24 started with `PATH=C:\Windows\System32;C:\Windows`, Qt plugin environment overrides cleared, `--storage-dir` directed to the isolated test data, and `--smoke-test`; exit code 0 and stderr empty.
- Silent uninstall exited 0, removed the application directory and uninstall registry entry, and preserved an external user-data marker. No stable uninstall registration existed before the isolated test.
- Inno Setup 6.7.3 compiled the installer successfully. It emitted the existing non-blocking `[UninstallRun]` warning about `RunOnceId`.

## Scope

Stage 178 ports project search persistence and filter-reset behavior. Qt imports the old project query, searches project name/description and restores the query on page navigation and restart. Reset clears search, overdue and pending-XP flags without changing sort order. The lifecycle test did not use the user's installed application or production workspace.
