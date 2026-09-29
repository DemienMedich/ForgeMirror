# ForgeMirror Qt 0.6.81 release verification

- Canonical version: root `VERSION` = `0.6.81`.
- Scope: stage 236 restores the administrator menu action to copy the detected stable project `data/` folder path.
- Windows per-user installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.81.exe`; 12,501,393 bytes; FileVersion/ProductVersion `0.6.81`; SHA-256 `BD7538DD6695B2797EA4C866923A54CBCCA327DB3CA1A784736B57A5DED70DEC`.
- Packaged Qt executable: `Z:\CPP\ForgeMirror\package-qt-0.6.81-release\ForgeMirrorQt.exe`; 3,507,200 bytes; FileVersion/ProductVersion `0.6.81`; `--version` output `ForgeMirrorQt 0.6.81`; SHA-256 `439F8A145F9F981B032966A9750A53EB0A1855504D7715F8C49352AAE5E02A1B`.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.81-release` passed. CTest `smoke_qt` passed 1/1 in 30.11 seconds; `smoke_core` reported `OK`. `smoke_qt` verifies the copy action is hidden from non-admin users, visible when an admin is authenticated and seed data exists, copies the exact native path and displays status feedback.
- The packaged EXE's informational `--version` command exited 0 and printed `ForgeMirrorQt 0.6.81` with Qt plugin override variables cleared and only Windows system directories in `PATH`.
- The actual Inno Setup 6.7.3 installer compiled successfully; its FileVersion and ProductVersion both report `0.6.81`.
- Isolated install lifecycle test used temporary AppId `{44FD9ECA-CB6E-42BE-8BB0-1E44E1C5C63E}` and per-user install path `C:\Users\mrdem\AppData\Local\Programs\ForgeMirrorQtLifecycle-b14e90b076ad4c1f92ff9edd3a4bc337`; production AppId and installation were not used. Version `0.6.80` installed, then `0.6.81` updated over it. Both uninstall entries and installed EXEs reported expected versions; both startup smoke tests exited 0 with Qt removed from `PATH`. Screenshots: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.81-b14e90b076ad4c1f92ff9edd3a4bc337\installed-0.6.80.png` (52,253 bytes) and `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.81-b14e90b076ad4c1f92ff9edd3a4bc337\installed-0.6.81.png` (52,213 bytes).
- Uninstall exited 0, removed the isolated application files and uninstall entry, and preserved the external user-data marker (`4A71ED90ED064ABD58287A88FD93B4F980B61B27FC399AF32FE9A64F3E30EC34`) and workspace marker (`658F4617A670D04F955928EAD9139785523FE238E109533EA988F8E1511AFE44`) byte-for-byte.
- `git diff --check` passed before commit. The user-edited `AgentsSkills/CONTINUITY.md` remains separate from this release.

Functional migration remains an expert estimate of about 95%, not a measured code or test percentage. The action-level parity audit is still open; this checkpoint does not declare the Qt port complete. Accessibility interaction testing and external writers that bypass the shared workspace lock remain follow-up work after feature parity.
