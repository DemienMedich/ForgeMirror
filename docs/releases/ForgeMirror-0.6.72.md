# ForgeMirror Qt 0.6.72 release verification

- Canonical version: root `VERSION` = `0.6.72`.
- Scope: stage 227 restores the legacy profile-statistics mini-bars for rank share and category averages. Qt keeps the numeric values in place, adds accessible names/descriptions, and keeps the existing purple accent.
- Windows installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.72.exe`; 11,573,037 bytes; FileVersion/ProductVersion `0.6.72`; SHA-256 `51EB241A6EC88232B3956D511F65C964B43A03DF9781151CF60B923EFD910CB6`.
- Packaged Qt executable: `Z:\CPP\ForgeMirror\package-qt-0.6.72-release\ForgeMirrorQt.exe`; 3,414,016 bytes; FileVersion/ProductVersion `0.6.72`; SHA-256 `9EB46029C7B001EFD088A57F5DA515E0D4B52D6D6796847D6B703073C7A2863C`.
- The production installer keeps AppId `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}` and per-user installation. Lifecycle verification compiled temporary installers with isolated AppId `{F8522A1E-0CAA-4418-9829-AB018469B5FC}` and installed under `build-qt\lifecycle-0.6.72`; the production installation and AppId were not used.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.72-release` passed. CTest `smoke_qt` passed 1/1 in 27.55 seconds; `smoke_core` reported `OK`. `smoke_qt` verifies both charts' numeric source cells, visible bar widths and accessibility metadata (50% rank share and 5.0/10 category average in the fixture); chart screenshots are in `build-qt\visual-stats-0.6.72-final`.
- Packaged startup with `PATH` restricted to Windows system directories and Qt plugin overrides removed exited 0; `build-qt\runtime-release-0.6.72.png` was saved (51,455 bytes). EXE FileVersion and ProductVersion match the installer version.
- The isolated 0.6.71 install registered version `0.6.71` and passed real-window startup with minimal `PATH` (exit 0). Updating in place to 0.6.72 changed both the registered and EXE versions to `0.6.72`; its startup smoke also passed (exit 0). External user-data and workspace marker SHA-256 values were unchanged after update and uninstall.
- Silent isolated uninstall exited 0, removed the installed EXE and uninstall registry entry, and preserved both marker files. No production install was touched.
- Installer compilation used Inno Setup 6.7.3. `git diff --check` passed before commit.
- The first package directory was open by a running Qt 0.6.72 process, so the finalized package was written to `package-qt-0.6.72-release`; no running application was stopped.

Functional migration remains an expert estimate of about 90%, not a code or test percentage. All 18 legacy workspace tabs have Qt surfaces, but action-level parity work continues. Hands-on NVDA/JAWS testing, wider non-UI core-event coverage, and older/external writers that bypass the shared lock remain follow-up items after migration parity.
