# ForgeMirror 0.6.25 Qt installer verification

- Canonical version: root `VERSION` = `0.6.25`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.25.exe`.
- Size: `11,517,014` bytes.
- SHA-256: `3260C73147C0BF03D327328D2BB4ABFE35D6CEA9DCC4C51CAC15783D3BA434C9`.
- Setup ProductVersion and packaged EXE ProductVersion/FileVersion: `0.6.25`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; current-user installation (`PrivilegesRequired=lowest`).
- Payload: 28 files.

## Verification

- `build-qt.ps1 -Package`: passed; `smoke_qt` 1/1 and `smoke_core: OK`.
- Administrator UI regression verifies wrong-password rejection, checkbox persistence, correct password preservation, logout clearing, repeated window restoration and password rotation. The added process integration test launches the sibling Qt executable three separate times in one disposable workspace; each startup restores administrator mode, preserves the credential record, and emits no rejected-login event.
- Isolated installer lifecycle directory: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage179-final-d184d49d8b0941a89823446ef2de2803`. Installed 0.6.24, updated in-place to 0.6.25 under the stable AppId, and verified EXE ProductVersion/FileVersion plus HKCU uninstall DisplayVersion.
- Installed 0.6.25 launched with `PATH=C:\Windows\System32;C:\Windows`, Qt plugin environment overrides cleared, disposable `--storage-dir`, `--screenshot` and `--smoke-test`; exit code 0, empty stderr, screenshot 45,484 bytes.
- Silent uninstall exited 0, removed the application directory and uninstall entry, and preserved a separate user-data marker. No stable uninstall registration existed before testing; the pre-existing 0.6.18 process remained running throughout.
- Inno Setup 6.7.3 compiled successfully. It emitted the existing non-blocking `[UninstallRun]` warning about `RunOnceId`.

## Administrator-session incident evidence

- The existing installed 0.6.18 executable was also tested in a separate temporary workspace across three process launches. Each exited 0 and recorded `Administrator session restored`; no rejected login was recorded. The live user's workspace was not used for this test.
- The user's actual workspace has `stayLoggedIn=1` and its current startup log records session restoration. This does not reproduce or explain the reported third-attempt password rejection. The login dialog's displayed workspace path remains necessary to distinguish a different workspace or launch route; real credentials were not read.

## Scope

Stage 179 adds process-level regression coverage for administrator session persistence. It also replaces a stale task-deletion error that claimed awarded-XP rollback was unimplemented, and corrects an obsolete pipeline note in the migration README. The user's running 0.6.18 application and production workspace were not upgraded or modified.
