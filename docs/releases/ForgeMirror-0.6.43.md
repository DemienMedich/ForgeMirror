# ForgeMirror Qt 0.6.43 installer verification

- Canonical version: root `VERSION` = 0.6.43.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.43.exe`.
- Size: 11,545,253 bytes.
- SHA-256: `02E49006A052A4073044DF011153F3266737AA0992B777A4FCD73AE38E28F437`.
- Installer and packaged EXE FileVersion/ProductVersion: 0.6.43.
- Production AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; per-user installation (`PrivilegesRequired=lowest`).
- Payload: 28 files (31,820,188 bytes in `package-qt-next3`).
- Compiler: Inno Setup 6.7.3; successful compile.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-next3` passed CTest (`smoke_qt` 1/1) and `smoke_core: OK`.
- Portable package `--version` returned `ForgeMirrorQt 0.6.43` with PATH limited to `C:\Windows\System32;C:\Windows` and Qt-related environment variables removed. Real-window startup smoke exited 0. Screenshot: `build-qt-next\package-smoke-0.6.43\startup.png` (51,507 bytes).
- Installed lifecycle used isolated test AppId `{04E3AED2-0AEF-4E41-BB83-719546E26A18}` under `build-qt-next\installer-test-stage-0.6.43-675ce576799f4d5c9331b86cbe79987d`. It installed 0.6.42, then updated the same directory to 0.6.43. EXE metadata and HKCU uninstall `DisplayVersion` matched at both versions; the install directory stayed unchanged.
- Installed 0.6.43 returned `ForgeMirrorQt 0.6.43` and completed a real-window startup smoke with Qt removed from PATH and isolated `LOCALAPPDATA`/`APPDATA`. Process exit code was 0; screenshot: `build-qt-next\installer-test-stage-0.6.43-675ce576799f4d5c9331b86cbe79987d\installed-smoke.png` (52,584 bytes).
- Silent uninstall exited 0, removed the test executable and registry entry, and preserved a marker in separate user data. The existing production package remained 0.6.40 with the same EXE SHA-256 (`9054EFA011B32584453497E64820C878E5365312F7A4D9DF4A070A114E427B2C`) before and after; no production workspace was used by the test.

## Scope

Stage 198 adds semantic accessible descriptions to the report, profile analytics, and log histogram charts. Estimated functional migration remains about 90%; this is a breadth-based estimate, not a code or test percentage. External NVDA/JAWS interaction checks, broader non-UI core telemetry, and support for live external writes during multi-file transactions remain open.
