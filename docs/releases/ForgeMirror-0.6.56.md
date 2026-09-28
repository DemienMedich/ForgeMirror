# ForgeMirror Qt 0.6.56 release verification

- Canonical version: root `VERSION` = `0.6.56`.
- Scope: restored the stable ImGui **Окно** menu in the Qt overflow menu. Checkable fullscreen and frameless actions share their handlers with F11/F10, persist settings in the isolated Qt workspace, and keep their check state synchronized with display-settings changes. The fixed palette and stable ImGui implementation are unchanged.
- Production installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.56.exe`; 11,561,484 bytes; SHA-256 `B2079AD8A760425A9EFE065BE7E7BC43D9B76964E16B158A20303A557BEF9DFE`.
- Packaged executable: `Z:\CPP\ForgeMirror\package-qt-0.6.56\ForgeMirrorQt.exe`; 3,369,472 bytes; FileVersion/ProductVersion `0.6.56`; SHA-256 `83E728F6DFE6B7482F68DE278D272C6673752BFE7734CD525F209CC5CF7B8D70`.
- Installer FileVersion/ProductVersion: `0.6.56`; compiled with Inno Setup 6.7.3. Production AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}` and installation remains per-user.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.56` completed. `smoke_qt` passed (22.65 sec), including menu membership, checkable state, F10/F11-to-menu synchronization, display-settings-to-menu synchronization, persisted fullscreen and frame state, native drag handle and restore after rebuilding the window. `smoke_core: OK`; `git diff --check` passed.
- The production installer was compiled from the canonical `VERSION` and matched the package version. An isolated test-only AppId `{D6CFA14D-5C4B-4D53-99A5-35545E3352D8}` at `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.56-20260928-window-menu` installed 0.6.55, then updated in place to 0.6.56. The installed executable reported FileVersion/ProductVersion `0.6.56`.
- The isolated installed executable opened its real window with Qt-related environment variables removed and `PATH=C:\Windows\System32;C:\Windows`; it exited normally after the smoke interval and saved `build-qt\lifecycle-0.6.56-20260928-window-menu\installed-window.png` (52,060 bytes).
- Silent uninstall exited 0, removed the installed executable and its test uninstall registry record, and preserved both an isolated Qt workspace marker and a marker under a separate app-local-data workspace. The production AppId and shortcuts were not used by the lifecycle test.
- The production current-user installation updated from 0.6.55 to 0.6.56. SHA-256 hashes for all 45 existing workspace files were captured before and after the installer; no paths or contents changed. The installed 0.6.56 app was then opened from `C:\Users\mrdem\AppData\Local\Programs\ForgeMirror\ForgeMirrorQt.exe` and recorded `Administrator session restored`.
- `develop` remains at `7306152c603ff8007200f64e63c4188510d55588`; the user's `AgentsSkills/CONTINUITY.md` change was not staged.

## Migration status

Stage 211 closes the stable window-menu path gap for fullscreen and frameless modes. Functional migration remains an expert estimate of about 90%, not a line-count or test percentage. Remaining verification and coordination gaps are described in `qt/README.md`.
