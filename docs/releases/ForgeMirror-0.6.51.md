# ForgeMirror Qt 0.6.51 installer verification

- Canonical version: root `VERSION` = `0.6.51`.
- Production installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.51.exe`.
- Installer size: 11,555,312 bytes; SHA-256: `4F534AE81ADC1D4732B6BEAFF9ACAAFBBA1E3E6D64F56D40E653ECD8DE1D744C`.
- Packaged EXE: `Z:\CPP\ForgeMirror\package-qt-next11\ForgeMirrorQt.exe`, 3,343,360 bytes; FileVersion/ProductVersion `0.6.51`; SHA-256: `764C7AB15EA69036070344E4BEADBF693B564B55D31E3D579AEEBBD47AE2A49C`.
- Installer ProductVersion: `0.6.51`. Compiled with Inno Setup 6.7.3. Production AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; per-user installation and persistent user data.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-next11` passed. `smoke_qt` passed 1/1; `smoke_core: OK`; Qt runtime deployment completed.
- Shared core also compiled and linked as the stable `ForgeMirrorGui` target from this branch; no ImGui UI implementation files were changed.
- Packaged EXE real-window `--smoke-test --screenshot` exited 0 with `PATH=C:\Windows\System32;C:\Windows`, Qt plugin environment overrides cleared, and separate `LOCALAPPDATA`, `APPDATA`, and workspace. Screenshot: `Z:\CPP\ForgeMirror\build-qt\package-smoke-0.6.51\window.png` (51,376 bytes).
- Production setup was compiled by `installer/build-qt-installer.ps1 -PackageDirectory .\package-qt-next11`; installer and application metadata both report `0.6.51`.
- Isolated lifecycle used test-only AppId `{1B10A9C0-130A-4CE3-8B6F-BF6EE0385F50}`, test install directory `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.51\installed`, and separate data/profile directories. Installed 0.6.50, then updated in place to 0.6.51. Installed EXE and HKCU uninstall `DisplayVersion` both reported `0.6.51`; a user-data marker survived update.
- Installed 0.6.51 passed real-window smoke with the minimal Windows PATH and cleared Qt environment overrides (exit 0); screenshot: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.51\installed-window.png` (51,768 bytes).
- Silent uninstall exited 0, removed the test EXE and uninstall registry entry, and preserved the user-data marker. The production installation was not targeted by this lifecycle test.
- `smoke_core` holds the task lock through a second OS handle; both conditional and unconditional task saves refuse without changing bytes while held, then a conditional retry commits successfully after release. This verifies contention handling through the shared save API; arbitrary direct file writes that ignore the lock and cross-file multi-file transactions remain outside its guarantee.

## Scope

Stage 206 serializes cooperating task saves across processes from stale-snapshot comparison through atomic task-file replacement. The persistent `meta/tasks.json.lock` sidecar is an internal workspace file. Task writes by binaries or tools that bypass the shared save APIs, and concurrent external multi-file transactions, are still not coordinated. Functional Qt migration remains an expert estimate of about 90%, not a measured code/test percentage; remaining parity work includes hands-on NVDA/JAWS testing and uninstrumented core events. Stable ImGui source, `develop` and production user data remain unchanged.
