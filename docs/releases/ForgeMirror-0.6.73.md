# ForgeMirror Qt 0.6.73 release verification

- Canonical version: root `VERSION` = `0.6.73`.
- Scope: stage 228 restores profile-manager parity for searching by name, ID, login and profession; filtering by all/active/archive and profession (including none); sorting by ID/name; and explicit list refresh. Profile profession is shown in the manager table. Inspection uses read-only snapshots so the current storage profile is not switched.
- Windows per-user installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.73.exe`; 11,577,166 bytes; FileVersion/ProductVersion `0.6.73`; SHA-256 `AF3C6FBBD177F40805EE5386739E255816B15C7813831C6CB4FE4BE35B02D862`.
- Packaged Qt executable: `Z:\CPP\ForgeMirror\package-qt-0.6.73-release\ForgeMirrorQt.exe`; 3,429,888 bytes; FileVersion/ProductVersion `0.6.73`; SHA-256 `DC2ECF8BC8AE50FBE4C43DECEA615194497BB53B35AE7A326C704FA11639EDCF`.
- The production installer retains AppId `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`, per-user installation, and the existing user-data location. Lifecycle installers used temporary AppId `{68D04FE4-982A-47B3-9913-BD234053AAF7}` and the isolated target `build-qt\lifecycle-0.6.73`; the production installation and registry entry were not used.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.73-release` passed. CTest `smoke_qt` passed 1/1; `smoke_core` reported `OK`. The new profile-manager UI test covers every filter, both sort modes, manual refresh after an external profile is added, and preservation of the active profile. UI captures are in `build-qt\visual-profile-manager-0.6.73`.
- Packaged startup used only Windows system directories in `PATH`; Qt plugin override variables were cleared. The real-window smoke exited 0 and saved `build-qt\runtime-smoke-exit-0.6.73.png` (50,945 bytes).
- Isolated lifecycle: 0.6.72 installed with exit 0 and its EXE/registry version matched. Its real-window startup smoke exited 0. Installing 0.6.73 over it exited 0; the installed EXE and registry both reported `0.6.73`; its startup smoke exited 0. The test install directory was removed by uninstall and the temporary uninstall registry entry was removed.
- External user-data marker SHA-256 stayed `C167C95F79C330BB4DEFF49DC458892F211103B1FCE6485CCEC46CE3AC53AFA6`; workspace marker SHA-256 stayed `DCE24C3C2C6E64184B2D6E771B2BCB5E6AC7E5CEF16BF17FB55D848DEFF9FC20` through update and uninstall.
- Inno Setup 6.7.3 compiled the production installer. `git diff --check` passed before commit. No active application process was stopped.

Functional migration remains an expert estimate of about 90%, not a measured code or test percentage. Feature-parity work continues in the action-level audit; screen-reader interaction and older/external writers that bypass the shared lock remain follow-up quality work after feature parity.
