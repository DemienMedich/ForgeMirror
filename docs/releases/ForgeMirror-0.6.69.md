# ForgeMirror Qt 0.6.69 release verification

- Canonical version: root `VERSION` = `0.6.69`.
- Scope: stage 224. The pipeline editor now submits its full candidate through `AppSavePipelineCandidate`, which validates the opening snapshot and live in-memory list against the latest normalized file under the workspace lock. Stale editors refresh the workspace and disable Save until reopened.
- Windows installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.69.exe`; 11,570,208 bytes; SHA-256 `68B85C3C580037742A2470D272DA4342E8C6DCF45D947BE8E5E3E2467BD0BD11`.
- Installer FileVersion/ProductVersion: `0.6.69`; built with Inno Setup 6.7.3. Production AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}` and installation is per-user.
- Packaged Qt executable: `Z:\CPP\ForgeMirror\package-qt-0.6.69\ForgeMirrorQt.exe`; 3,412,480 bytes; FileVersion/ProductVersion `0.6.69`; SHA-256 `689DB6B2578689DC31579DAEFF442A663ED884128CD3684F6BB2A847EF13D47A`.
- ImGui compatibility executable, rebuilt from the same version: `Z:\CPP\ForgeMirror\build-gui\Release\ForgeMirrorGui.exe`; 2,978,816 bytes; FileVersion/ProductVersion `0.6.69`; SHA-256 `E461A8648564C61BF73599B595AD4B6A2A2A69F4F0A3DC2950BBB1D9AE36F0E3`.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.69` passed. CTest `smoke_qt` passed 1/1 in 29.06 seconds and `smoke_core` reported `OK`.
- `smoke_core` verifies stale full-list candidates are rejected without replacing external edits, refresh the live list, and can be saved after reloading. The Qt test edits the pipeline file while the real editor dialog is open, verifies the concurrent title/description survive, confirms Save is disabled, then reopens and saves successfully.
- Packaged `ForgeMirrorQt.exe --version` reported `ForgeMirrorQt 0.6.69` with `PATH` limited to Windows system directories and Qt environment overrides removed. The real-window smoke exited 0 with empty stderr and saved `Z:\CPP\ForgeMirror\build-qt\package-smoke-0.6.69-9acb545027ef4db9913fc0a73387f907\window.png` (52,165 bytes).
- Isolated installer lifecycle used temporary AppId `{CD8811C0-2EC4-45E9-AD0F-BA3BF9B62BAA}` in `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.69-a146d065f66e45d8932c4887a9c8a3e8\install`; the production AppId and installation were not used. Test installers 0.6.68 and 0.6.69 were compiled with the same packaged application files and that isolated AppId.
- The isolated 0.6.68 install registered `DisplayVersion=0.6.68`; its EXE reported `ForgeMirrorQt 0.6.68` and real-window smoke exited 0 (`installed-0.6.68.png`, 52,177 bytes). Updating the same directory and AppId to 0.6.69 changed both the executable ProductVersion and HKCU uninstall `DisplayVersion` to `0.6.69`; the external workspace manifest was identical before and after update.
- The installed 0.6.69 EXE reported `ForgeMirrorQt 0.6.69` and passed real-window smoke with Qt removed from `PATH` (`installed-0.6.69.png`, 52,175 bytes). Uninstall exited 0, removed the isolated executable and HKCU uninstall entry, and preserved the external workspace manifest. Its independent user-data marker remained SHA-256 `1DAF82F62247F3A1D148C2D88B1828C9EFA2D5F087D7059E98650AAFE7AFDEA3`.
- The package directory contains no developer `data` directory. No PharosHub manifest is used by this checkout. Production installation was not updated, and `develop` / `origin/develop` remain at `7306152c603ff8007200f64e63c4188510d55588`.
- `git diff --check` passed before release commit.

The Qt migration remains an expert estimate of about 90%, not a code or test percentage. Hands-on NVDA/JAWS interaction certification, wider non-UI core event coverage, and coordination with older or external writers that bypass the shared lock remain open.
