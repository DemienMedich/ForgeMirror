# ForgeMirror Qt 0.6.42 installer verification

- Canonical version: root `VERSION` = 0.6.42.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.42.exe`.
- Size: 11,543,072 bytes.
- SHA-256: `9292C38FB2CFCF6DFCB99E53A86328D6DC5F5F62BE01BE2A52A56BB1953C292F`.
- Installer and packaged EXE FileVersion/ProductVersion: 0.6.42.
- Production AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; per-user installation (`PrivilegesRequired=lowest`).
- Payload: 28 files (31,814,044 bytes in `package-qt-next2`).
- Compiler: Inno Setup 6.7.3; successful compile.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-next2` configured and built Qt, ran CTest (`smoke_qt` 1/1), and ran `smoke_core: OK`.
- The smoke audited accessible names/descriptions in 24 modal instances across existing UI workflows. The audit exposed and fixed missing semantics for cloud/storage comparison tables, achievements, profession bindings, profile history/export controls, and XP-rule presets/history.
- Portable package `--version` returned `ForgeMirrorQt 0.6.42` with PATH limited to `C:\Windows\System32;C:\Windows` and Qt-related environment variables removed. Real-window startup smoke exited 0. Screenshot: `build-qt-next\package-smoke-0.6.42\startup.png` (51,503 bytes).
- Isolated lifecycle used test AppId `{98736D2B-A89E-4512-8CD4-974261E692F8}` under `build-qt-next\installer-test-stage-0.6.42-c7b7c9eb832547678af87fc3624944ce`. It installed 0.6.41, then updated the same isolated directory to 0.6.42. EXE metadata and HKCU uninstall `DisplayVersion` matched at both versions; the install directory stayed unchanged.
- Installed 0.6.42 returned `ForgeMirrorQt 0.6.42` and completed a real-window startup smoke with Qt removed from PATH and an isolated `LOCALAPPDATA`/`APPDATA`. Screenshot: `build-qt-next\installer-test-stage-0.6.42-c7b7c9eb832547678af87fc3624944ce\installed-smoke\installed-window.png` (52,673 bytes).
- Silent uninstall exited 0, removed the test executable and registry entry, and preserved a marker in separate user data. The existing production ForgeMirror install remained 0.6.37 with the same EXE SHA-256 before and after; no production workspace was used by the test.

## Scope

Stage 197 expands static accessibility-name/description auditing across the 24 modal instances reached by smoke tests. Estimated functional migration remains about 90%; this is a breadth-based estimate, not a code or test percentage. External NVDA/JAWS interaction checks, broader non-UI core telemetry, and support for live external writes during multi-file transactions remain open.
