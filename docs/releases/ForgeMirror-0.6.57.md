# ForgeMirror Qt 0.6.57 release verification

- Canonical version: root `VERSION` = `0.6.57`.
- Scope: the Qt cloud page now displays the manifest's data update timestamp using the stable client's `yyyy-MM-dd HH:mm` format. A missing timestamp displays `—`; the fixed palette and stable ImGui implementation are unchanged.
- Production installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.57.exe`; 11,560,904 bytes; SHA-256 `6284B157362DF507BA3658B86771083BB671FEB1BCD8E7C12FC0432E0F890E09`.
- Packaged executable: `Z:\CPP\ForgeMirror\package-qt-0.6.57\ForgeMirrorQt.exe`; 3,369,984 bytes; FileVersion/ProductVersion `0.6.57`; SHA-256 `2FBAA30020531780170DFD70B4E4EBCDEDC915D477D3F8475DE665BABDFDEAEE`.
- Installer FileVersion/ProductVersion: `0.6.57`; compiled with Inno Setup 6.7.3. Production AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}` and installation remains per-user.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.57` completed. `smoke_qt` passed (25.01 sec), including the cloud manifest timestamp check against a known epoch; `smoke_core: OK`.
- The production installer was compiled from the canonical `VERSION` and matched the packaged executable version. A test-only installer with AppId `{D6CFA14D-5C4B-4D53-99A5-35545E3352D8}` at `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.57-20260928-cloud-stamp` installed 0.6.56, then updated in place to 0.6.57. The uninstall record and EXE metadata both reported 0.6.57 after update.
- The isolated installed executable opened its real window with `PATH=C:\Windows\System32;C:\Windows` and no Qt development environment variables. It exited normally after the smoke interval and saved `installed-window.png` (52,287 bytes).
- Silent uninstall exited 0, removed the installed executable and test uninstall record, and preserved marker files under both the isolated workspace and isolated `%LOCALAPPDATA%` application-data directory. No deadline schedule existed before the test uninstall.
- The production current-user installation updated from 0.6.56 to 0.6.57 at `C:\Users\mrdem\AppData\Local\Programs\ForgeMirror\ForgeMirrorQt.exe`; the running process showed window title `ForgeMirror · Qt migration · 0.6.57`. SHA-256 hashes for all 46 existing Qt workspace files and all 46 files in the Qt app-local-data tree were compared immediately before and after installer execution: no paths or contents changed.
- `develop` remains at `7306152c603ff8007200f64e63c4188510d55588`; the user's `AgentsSkills/CONTINUITY.md` change was not staged.

## Migration status

Stage 212 adds cloud-manifest freshness information to the Qt page. Functional migration remains an expert estimate of about 90%, not a line-count or test percentage. Remaining verification and coordination gaps are described in `qt/README.md`.
