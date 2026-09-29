# ForgeMirror Qt 0.6.77 release verification

- Canonical version: root `VERSION` = `0.6.77`.
- Scope: stage 232 restores the profile summary's active achievement count and summed active XP bonus, plus the recent three achievement preview with expiry state and overflow count.
- Windows per-user installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.77.exe`; 12,495,713 bytes; FileVersion/ProductVersion `0.6.77`; SHA-256 `F6905C25CA142726B04EB90B582632DA9338AA1162205F895A63C95F2B6C8A65`.
- Packaged Qt executable: `Z:\CPP\ForgeMirror\package-qt-0.6.77-release\ForgeMirrorQt.exe`; 3,482,624 bytes; FileVersion/ProductVersion `0.6.77`; `--version` output `ForgeMirrorQt 0.6.77`; SHA-256 `26040E5C8A817FFD6D1E67075CDD162012D105978CEDA223C303E153DA6931D3`.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.77-release` passed. CTest `smoke_qt` passed 1/1 in 28.21 seconds; `smoke_core` reported `OK`. The Qt fixture checks skill/achievement totals, active-only XP bonus, award-time ordering, expired and active states, focus-mode visibility, and the overflow count.
- The packaged executable's informational `--version` command exited 0 and printed `ForgeMirrorQt 0.6.77` with Qt plugin override variables cleared and only Windows system directories in `PATH`. File metadata independently reports `0.6.77` for both file and product version.
- The actual Inno Setup 6.7.3 installer compiled successfully; its FileVersion/ProductVersion both report `0.6.77`.
- Isolated lifecycle test used temporary AppId `{CEC80CD1-7110-4B57-BE35-DE7DA03A8801}` and per-user install path `C:\Users\mrdem\AppData\Local\Programs\ForgeMirrorQtLifecycle-54d56b7b12334aae85f5beb63bf58678`; the production AppId and installation were not used. Version `0.6.76` installed, then `0.6.77` updated over it. Both uninstall entries and installed EXEs reported their expected versions; startup smoke tests for both exited 0 with Qt removed from `PATH`. Screenshots: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.77-54d56b7b12334aae85f5beb63bf58678\installed-0.6.76.png` (51,546 bytes) and `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.77-54d56b7b12334aae85f5beb63bf58678\installed-0.6.77.png` (51,565 bytes).
- Uninstall exited 0, removed the isolated application files and uninstall entry, and preserved the external user-data marker (`4A71ED90ED064ABD58287A88FD93B4F980B61B27FC399AF32FE9A64F3E30EC34`) and workspace marker (`658F4617A670D04F955928EAD9139785523FE238E109533EA988F8E1511AFE44`) byte-for-byte.
- `git diff --check` passed before commit. The user-edited `AgentsSkills/CONTINUITY.md` remains separate from this release.

Functional migration remains an expert estimate of about 93%, not a measured code or test percentage. The action-level parity audit is still open; this checkpoint does not declare the Qt port complete. Accessibility interaction testing and external writers that bypass the shared workspace lock remain follow-up work after feature parity.
