# ForgeMirror Qt 0.6.68 release verification

- Canonical version: root `VERSION` = `0.6.68`.
- Scope: stage 223. Pipeline add/edit/delete/reorder now compare the caller's normalized snapshot to disk while holding the shared workspace lock; stale edits refresh in-memory state and fail without replacing concurrent changes.
- Windows installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.68.exe`; 11,570,450 bytes; SHA-256 `3E349CAE839E9632931C9C697C8892BC50385ACE3DFDDD5C4D01F462CB6F5D51`.
- Installer FileVersion/ProductVersion: `0.6.68`; built with Inno Setup 6.7.3. Production AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}` and installer mode remains per-user.
- Packaged Qt executable: `Z:\CPP\ForgeMirror\package-qt-0.6.68\ForgeMirrorQt.exe`; 3,410,432 bytes; FileVersion/ProductVersion `0.6.68`; SHA-256 `AD99341AE5A8679CFA1764CCC39A73C8E7305622DDE69AF71B2D5C2625C5A213`.
- ImGui compatibility executable, rebuilt from the same version: `Z:\CPP\ForgeMirror\build-gui\Release\ForgeMirrorGui.exe`; 2,978,816 bytes; FileVersion/ProductVersion `0.6.68`; SHA-256 `B46E1A1DB771B46951B0271368543A694E01452E03566721B5F07613AE893007`.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.68` passed. CTest `smoke_qt` passed 1/1 in 28.11 seconds and `smoke_core` reported `OK`.
- Packaged `ForgeMirrorQt.exe --version` reported `ForgeMirrorQt 0.6.68` with `PATH` restricted to Windows system directories and Qt environment overrides removed. The real-window `--smoke-test` exited 0, saved `Z:\CPP\ForgeMirror\build-qt\package-smoke-0.6.68-d168fd855c68410cba6b40dad91a14b5\window.png` (51,598 bytes), and produced empty stderr.
- Isolated update lifecycle used temporary AppId `{1E527EB5-490B-4264-BC16-82B8B6A8385C}` in `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.68-4d28c89c538a4ebb93f412faf88c6d3f\install`; the production AppId and installation were not used. Temporary 0.6.67 and 0.6.68 installers were compiled from the release script with the isolated AppId and the same package files.
- The isolated 0.6.67 install registered `DisplayVersion=0.6.67`; its installed EXE reported `ForgeMirrorQt 0.6.67` and real-window smoke exited 0 (`installed-0.6.67.png`, 52,130 bytes). Installing 0.6.68 over the same directory and AppId updated both the EXE ProductVersion and HKCU uninstall `DisplayVersion` to `0.6.68`.
- The workspace manifest SHA-256 `4A501AA1053DC3CC2353A4F4A28DE5E2E0E92767093D00D4A85B432D43E729F7` was identical immediately before and after the in-place update. The installed 0.6.68 executable passed real-window smoke with Qt removed from `PATH` (`installed-0.6.68.png`, 52,048 bytes).
- Uninstall exited 0, removed the isolated executable and HKCU uninstall registration, and preserved the external test workspace exactly as measured immediately before uninstall. Its independent marker remained SHA-256 `94E093A2DEFD81BC6FED08AE55C1D208950A8148DB25EFB95927C461092A5D72`.
- The package directory contains no developer `data` directory. No PharosHub manifest is used by this checkout. Production installation was not updated, and `develop` / `origin/develop` remain at `7306152c603ff8007200f64e63c4188510d55588`.
- `git diff --check` passed before release commit.

The Qt migration remains an expert estimate of about 90%, not a code or test percentage. Hands-on NVDA/JAWS interaction certification, wider non-UI core event coverage, and coordination with older or external writers that bypass the shared lock remain open.
