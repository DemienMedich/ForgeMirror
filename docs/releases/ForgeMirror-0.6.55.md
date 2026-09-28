# ForgeMirror Qt 0.6.55 release verification

- Canonical version: `VERSION` = `0.6.55`.
- Scope: profile-credential parity. Profile creation and administrator password reset provide explicit copy actions; password copy remains disabled until reveal. Administrator reset generates a random password, requires confirmation, and keeps the transactional result visible until the user closes the dialog. The guided pipeline transition selector also exposes its purpose to accessibility APIs. Credentials are not written to logs.
- Production installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.55.exe`; 11,561,137 bytes; SHA-256 `8AFA972AAF7B5FE507D345D2CA71AC1C6F8AC7E3E4E1A24A64A94F51ACF40197`.
- Packaged executable: `Z:\CPP\ForgeMirror\package-qt-0.6.55\ForgeMirrorQt.exe`; 3,364,864 bytes; FileVersion/ProductVersion `0.6.55`; SHA-256 `73A92FA98CDB5067D3452F0A4AB8D2019290B824CAA9C2B38D5782F77D9DC9D1`.
- Installer FileVersion/ProductVersion: `0.6.55`; compiled with Inno Setup 6.7.3. Production AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}` and installation remains per-user.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.55` completed; `smoke_qt` passed 1/1 (22.43 sec), including credential reveal/copy, reset confirmation, transactional rollback, subsequent personal password change, and dialog accessibility checks; `smoke_core: OK`. Packaged `--version` reports `ForgeMirrorQt 0.6.55` and exits 0.
- Isolated lifecycle used test-only AppId `{D6CFA14D-5C4B-4D53-99A5-35545E3352D8}` and test root `Z:\CPP\ForgeMirror\build-qt\lifecycle-finalrelease-0.6.55-20260928153114`. Version 0.6.54 installed and updated in place to 0.6.55. The installed EXE opened its real window with Qt environment variables removed and `PATH=C:\Windows\System32;C:\Windows` (exit 0; screenshot `Z:\CPP\ForgeMirror\build-qt\lifecycle-finalrelease-0.6.55-20260928153114\installed-window.png`, 52,186 bytes). Silent uninstall exited 0, removed the test EXE and uninstall registry entry, and preserved `workspace/user-data-marker.txt`.
- The current-user production installation was updated from 0.6.54 to 0.6.55 using the production AppId. Its administrator settings file hash was unchanged across installation; after relaunch the application log recorded `Administrator session restored`. The installed EXE SHA-256 matches the packaged executable above; it was left running.
- Stable ImGui source and `develop` were not modified. The user's `AgentsSkills/CONTINUITY.md` working-tree change was not staged.

## Migration status

Stage 210 restores credential-copy behavior from the stable profile creation/reset flows and closes a missing accessible name in pipeline transitions. Functional migration remains an expert estimate of about 90%, not a line-count or test percentage. Remaining parity work includes hands-on NVDA/JAWS interaction testing, broader non-UI core event coverage, and coordination with older or external writers that bypass shared save APIs.
