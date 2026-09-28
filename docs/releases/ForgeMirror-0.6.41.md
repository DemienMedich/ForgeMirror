# ForgeMirror Qt 0.6.41 installer verification

- Canonical version: root VERSION = 0.6.41.
- Installer: Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.41.exe.
- Size: 11,540,844 bytes.
- SHA-256: 9499DB1F70E8E9B00CBFCED180FF640B9648C0F8A3799E10B901C7C84C63F864.
- Installer FileVersion/ProductVersion and packaged EXE FileVersion/ProductVersion: 0.6.41.
- Production AppId: {8B99E76B-4510-49D8-AE45-9DDF85EA21DC}; per-user installation (PrivilegesRequired=lowest).
- Payload: 28 files (31,806,876 bytes in package-qt-next).
- Compiler: Inno Setup 6.7.3; successful compile without warnings.

## Verification

- Configured and built in the separate build-qt-next directory. CTest passed smoke_qt 1/1; smoke_core returned OK.
- Task XP completion emits generic validation, rollback/failure, and committed events through an optional AppContext sink. Qt connects its existing task-completion log callback to the core service, removing duplicate log calls. Tests verify event text excludes task/profile names and IDs and that a throwing sink does not affect a successful transaction.
- Packaged EXE completed a real-window startup smoke with PATH restricted to C:\Windows\System32;C:\Windows and Qt plugin environment variables removed. Screenshot: build-qt-next\package-smoke\startup-retry.png (51,247 bytes).
- Isolated per-user lifecycle used test AppId {8D5F27F6-29C3-49AE-9413-7512325E95D8} under build-qt-next\installer-test-stage. Installed 0.6.40, then updated the same isolated directory in place to 0.6.41. EXE metadata and HKCU uninstall DisplayVersion matched at both steps; the install path remained isolated.
- Installed 0.6.41 returned ForgeMirrorQt 0.6.41 and completed real-window startup smoke with Qt-related PATH and plugin environment variables removed. Screenshot: build-qt-next\installer-test-stage\installed-window.png (51,245 bytes).
- Silent uninstall removed the test EXE and registration. A marker in separate test user data remained. The installed 0.6.37 application, production workspace, stable ImGui installation and develop were not modified.

## Scope

Stage 196 adds optional privacy-safe task-completion outcome telemetry. Estimated functional migration remains about 90%; remaining work includes a full screen-reader audit of complex dialogs, broader non-UI core telemetry, and protection against live external edits during multi-file transactions.
