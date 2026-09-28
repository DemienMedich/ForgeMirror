# ForgeMirror Qt 0.6.52 installer verification

- Canonical version: root `VERSION` = `0.6.52`.
- Production installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.52.exe`; 11,554,680 bytes; SHA-256 `CC9C926FED723675632131E310B867875B01A45A8C95349DC1DA7622FB3F7712`.
- Packaged EXE: `Z:\CPP\ForgeMirror\package-qt-0.6.52\ForgeMirrorQt.exe`; 3,344,896 bytes; FileVersion/ProductVersion `0.6.52`; SHA-256 `014DE5DD38BBD49D05B1DE080016C41C61543322AF0C3017196E549FE1051118`.
- Installer FileVersion/ProductVersion: `0.6.52`. Compiled with Inno Setup 6.7.3. Production AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; per-user install and user data remain separate.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.52` completed. `smoke_qt` passed 1/1 and `smoke_core` reported `OK`; `windeployqt` populated the package-local Qt runtime and plugins.
- Packaged executable opened the real window and exited 0 with `PATH=C:\Windows\System32;C:\Windows`, Qt plugin environment overrides removed, and isolated `LOCALAPPDATA`, `APPDATA` and workspace. Screenshot: `Z:\CPP\ForgeMirror\build-qt\package-smoke-0.6.52\window.png` (51,504 bytes).
- The stable `ForgeMirrorGui` target compiled successfully against the shared-core profile lock changes. No ImGui UI source was changed.
- Isolated install/update lifecycle used test-only AppId `{D6CFA14D-5C4B-4D53-99A5-35545E3352D8}`, install directory `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.52\installed`, and separate user-data/workspace directories. The test-only 0.6.51 installer installed with exit 0; the 0.6.52 installer updated it in place with exit 0. Installed EXE and HKCU uninstall `DisplayVersion` changed from `0.6.51` to `0.6.52`; a user-data marker survived.
- Installed 0.6.52 passed real-window smoke with the minimal Windows `PATH` and Qt plugin environment overrides cleared (exit 0); screenshot: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.52\installed-window.png` (51,657 bytes).
- Silent uninstall exited 0, removed the test EXE and test uninstall entry, and preserved the user-data marker. The test AppId and isolated install were removed; the production AppId was not used by this lifecycle test.
- `smoke_core` verifies a second OS handle blocks profile saves without changing profile bytes, and retry succeeds after release. It also verifies `meta/profile-write.lock` is not reported as a stray workspace item.
- The current-user production installation and its open 0.6.51 process were not updated by this release test. Production user data and `develop` were not modified.

## Scope

Stage 207 serializes profile persistence operations across cooperating clients that use the shared `FileStorage`: stale compare-and-replace, achievement persistence, profile archive/restore, deletion and profile-ID normalization use `meta/profile-write.lock`. Older binaries and tools that bypass `FileStorage`, and cross-file transactions involving external writers, remain outside the lock guarantee. Functional Qt migration remains an expert estimate of about 90%, not a measured code/test percentage; a manual NVDA/JAWS interaction audit and broader core event coverage remain open.
