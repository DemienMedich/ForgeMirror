# ForgeMirror Qt 0.6.39 installer verification

- Canonical version: root `VERSION` = `0.6.39`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.39.exe`.
- Size: `11,541,191` bytes.
- SHA-256: `A3FE14632B82EEC47682CA53039F8D0FBB9B047BDB102EBD57EED3DE4FA67D4A`.
- Installer FileVersion/ProductVersion and packaged EXE FileVersion/ProductVersion: `0.6.39`.
- Production AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; per-user install (`PrivilegesRequired=lowest`).
- Payload: 28 files (31,804,316 bytes in the packaged directory).
- Compiler: Inno Setup 6.7.3; successful compile without warnings after adding the uninstall `RunOnceId`.

## Verification

- `build-qt.ps1 -Package` passed: `smoke_qt` 1/1 and `smoke_core: OK`.
- `package-qt/ForgeMirrorQt.exe` was launched with `PATH=C:\Windows\System32;C:\Windows` and isolated workspaces at 100% and 200%. Both smoke runs created screenshots (`build-qt/accessibility-scale-check/normal.png`, `large.png`); the 200% screenshot showed enlarged typography/controls and vertical content scrolling. The application palette remained unchanged.
- An isolated test installer with temporary AppId `{3824D636-F321-42C6-B4E8-BCA3950CFC04}` was installed at `build-qt/installer-lifecycle-0.6.39-final-8e64bf80fd5a4048820acaef8d1d8f77/isolated-install`: 0.6.38 installed first, then 0.6.39 upgraded it at the same path. The uninstall registration's `DisplayVersion` and installed EXE ProductVersion both reported `0.6.39`.
- The installed EXE returned `ForgeMirrorQt 0.6.39` from `--version` and completed `--smoke-test --screenshot` with Qt removed from `PATH` (`C:\Windows\System32;C:\Windows`). Screenshot: `build-qt/installer-lifecycle-0.6.39-final-8e64bf80fd5a4048820acaef8d1d8f77/startup.png` (53,427 bytes).
- Silent uninstall removed the installed payload, empty install directory and uninstall registration. A marker in the separate test user workspace survived. The production 0.6.37 installation, production workspace, stable ImGui installation and `develop` were not modified by lifecycle verification.

## Scope

Stage 194 adds selectable 150%, 175% and 200% text scaling with larger fixed controls and scrollable content. The estimated Qt migration remains about 90%; screen-reader coverage, non-UI core telemetry and support for live external writes during a multi-file transaction remain open gaps.
