# ForgeMirror Qt 0.6.66 release verification

- Canonical version: root `VERSION` = `0.6.66`.
- Scope: stage 221. Qt application-log appends now merge pending entries with the latest disk history under the workspace lock; clearing the log requires explicit confirmation.
- Windows installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.66.exe`; 11,566,674 bytes; SHA-256 `D146FAD1BCC325C90D5711370004CD864771CD44513A85FCBEBCCF200CECE785`.
- Installer FileVersion/ProductVersion: `0.6.66`; built with Inno Setup 6.7.3. Production AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; installation is per-user.
- Packaged executable: `Z:\CPP\ForgeMirror\package-qt-0.6.66\ForgeMirrorQt.exe`; 3,393,536 bytes; FileVersion/ProductVersion `0.6.66`; SHA-256 `CA9EF6CB81A20DE0FF03C60333B85EB342312402BDEBF0E604973D262D31BC6B`.
- ImGui compatibility executable rebuilt: `Z:\CPP\ForgeMirror\build-gui\Release\ForgeMirrorGui.exe`; 2,960,384 bytes; FileVersion/ProductVersion `0.6.66`; SHA-256 `F0712F6481E510C6EFF795B7912D6C025E0ACB7CC80F05A0A02C4B31DA2D76A3`.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.66` passed. `smoke_qt` passed 1/1 in 33.60 seconds, including the real-dialog remembered-administrator login followed by three independent process launches; `smoke_core: OK`.
- `ForgeMirrorGui` rebuilt successfully from the same canonical `VERSION`; its EXE metadata reports `0.6.66`.
- The packaged executable reported `ForgeMirrorQt 0.6.66`. With `PATH=C:\Windows\System32;C:\Windows` and Qt environment overrides removed, its real-window smoke exited 0 and saved `Z:\CPP\ForgeMirror\build-qt\package-smoke-0.6.66-retry-81614963eadc4a61b8bf140c842c727c\window.png` (52,308 bytes).
- The per-user installer compiled successfully. An isolated lifecycle copy used temporary AppId `{0B0CF327-995F-49F4-A1C2-D79117C6544B}` and install directory `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.66-3ba9faae88934dd1b198b4105da15b88\install`; it did not target the production installation.
- The isolated 0.6.65 installer installed successfully, then 0.6.66 updated the same installation in place. At both steps the installed EXE ProductVersion and HKCU uninstall `DisplayVersion` matched. The updated installed EXE passed a real-window smoke with Qt removed from `PATH` (exit 0; screenshot 52,198 bytes).
- Silent uninstall exited 0, removed the isolated executable and uninstall registry entry, and left an external user-data marker unchanged: SHA-256 `E3437F6B61D1749F7F29F17A53E7FDFE53210430988BFBDB8C171B9E1A530741`.
- The separate current-user installation was then updated from `0.6.60` to `0.6.66` and reopened as PID `7404`, title `ForgeMirror · Qt migration · 0.6.66`. Its installed EXE and uninstall record match `0.6.66`. Before/after aggregate SHA-256 inventories of Qt LocalAppData and legacy ForgeMirror RoamingAppData were identical: 89 files, 1,006,899 bytes, manifest SHA-256 `DE967E46A7D29F64B54C4BC1738EBA01B9F3A0263517C4D6ED993096A9D8CF37`.
- The default Qt workspace's non-secret `stayLoggedIn=1` preference was present before update, and the updated app logged `Administrator session restored`; no password value was read. If a login prompt still appears, compare the workspace path shown in that dialog with the default Qt workspace: the currently installed app is restored from the default workspace.
- `develop` and `origin/develop` remain `7306152c603ff8007200f64e63c4188510d55588`. The production installation was not used for isolated lifecycle testing. No workspace, profile, cloud configuration, or developer-local storage is bundled.

The Qt migration remains an estimated 90% complete. This is a feature-coverage estimate, not a percentage of files or tests; manual NVDA/JAWS certification and the documented external-writer/core-telemetry gaps remain open.
