# ForgeMirror Qt 0.6.44 installer verification

- Canonical version: root `VERSION` = 0.6.44.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.44.exe`.
- Size: 11,546,985 bytes.
- SHA-256: `EA419936175AB76C027D6DC48DDC3D2AC9DC3E100AB7F72DB09557DC315F56A4`.
- Installer and packaged EXE FileVersion/ProductVersion: 0.6.44.
- Production AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; per-user installation (`PrivilegesRequired=lowest`).
- Payload: 28 files (31,823,260 bytes in `package-qt-next4`).
- Compiler: Inno Setup 6.7.3; successful compile.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-next4` passed CTest (`smoke_qt` 1/1) and `smoke_core: OK`.
- Profile and task transaction wrappers now report generic success/failure outcomes through optional exception-isolated core event sinks. Smoke verifies commit and rollback paths for profile edits, password changes, rules reapplication and direct XP, including that profile IDs and entered passwords never enter event messages.
- Portable package `--version` returned `ForgeMirrorQt 0.6.44` with PATH limited to `C:\Windows\System32;C:\Windows` and Qt-related environment variables removed. Real-window startup smoke exited 0 with empty stderr. Screenshot: `build-qt-next\package-smoke-0.6.44\startup.png` (51,641 bytes).
- Isolated lifecycle used test AppId `{04E3AED2-0AEF-4E41-BB83-719546E26A18}` under `build-qt-next\installer-test-stage-0.6.44-04e3aed20aef4e41bb83719546e26a18`. It installed 0.6.43, then updated the same directory to 0.6.44. EXE metadata and HKCU uninstall `DisplayVersion` matched at both versions; the install directory stayed unchanged.
- Installed 0.6.44 returned `ForgeMirrorQt 0.6.44` and completed a real-window startup smoke with Qt removed from PATH and isolated `LOCALAPPDATA`/`APPDATA`. Process exit code was 0; screenshot: `build-qt-next\installer-test-stage-0.6.44-04e3aed20aef4e41bb83719546e26a18\installed-smoke.png` (52,907 bytes).
- Silent uninstall exited 0, removed the test executable and registry entry, and preserved a marker in separate user data. The existing production install remained 0.6.37 with the same EXE SHA-256 before and after; no production workspace was used by the test.

## Scope

Stage 199 wires more journaled profile/task mutation outcomes into the administrator Audit source without duplicating UI logging. Estimated functional migration remains about 90%; this is a breadth-based estimate, not a code or test percentage. External NVDA/JAWS interaction checks, wider core-service event coverage, and live external writers during multi-file transactions remain open.
