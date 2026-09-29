# ForgeMirror Qt 0.6.80 release verification

- Canonical version: root `VERSION` = `0.6.80`.
- Scope: stage 235 restores the legacy copy actions for the selected profile's name, ID and login in the Qt header.
- Windows per-user installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.80.exe`; 12,500,999 bytes; FileVersion/ProductVersion `0.6.80`; SHA-256 `AB228DCC632169AAFDE88DAB2095E595F0428EB195F44FD269FB53013A4EFF7E`.
- Packaged Qt executable: `Z:\CPP\ForgeMirror\package-qt-0.6.80-release\ForgeMirrorQt.exe`; 3,498,496 bytes; FileVersion/ProductVersion `0.6.80`; `--version` output `ForgeMirrorQt 0.6.80`; SHA-256 `AF3C3DE5A0D0F62D77CC9156FF7B5BC3D7DE01CBF78F00BFFB82DED073E02F93`.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.80-release` passed. CTest `smoke_qt` passed 1/1 in 26.46 seconds; `smoke_core` reported `OK`. `smoke_qt` checks that all three actions copy the selected profile's exact name, ID and login and show status-bar confirmation.
- The packaged EXE's informational `--version` command exited 0 and printed `ForgeMirrorQt 0.6.80` with Qt plugin override variables cleared and only Windows system directories in `PATH`.
- The actual Inno Setup 6.7.3 installer compiled successfully; its FileVersion and ProductVersion both report `0.6.80`.
- Isolated install lifecycle test used temporary AppId `{FAA9802B-55DC-4253-ABD4-29AE6F55F280}` and per-user install path `C:\Users\mrdem\AppData\Local\Programs\ForgeMirrorQtLifecycle-b9d9bde2a5aa429d8a9480154befcc97`; production AppId and installation were not used. Version `0.6.79` installed, then `0.6.80` updated over it. Both uninstall entries and installed EXEs reported expected versions; both startup smoke tests exited 0 with Qt removed from `PATH`. Screenshots: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.80-b9d9bde2a5aa429d8a9480154befcc97\installed-0.6.79.png` (51,359 bytes) and `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.80-b9d9bde2a5aa429d8a9480154befcc97\installed-0.6.80.png` (52,170 bytes).
- Uninstall exited 0, removed the isolated application files and uninstall entry, and preserved the external user-data marker (`4A71ED90ED064ABD58287A88FD93B4F980B61B27FC399AF32FE9A64F3E30EC34`) and workspace marker (`658F4617A670D04F955928EAD9139785523FE238E109533EA988F8E1511AFE44`) byte-for-byte.
- `git diff --check` passed before commit. The user-edited `AgentsSkills/CONTINUITY.md` remains separate from this release.

Functional migration remains an expert estimate of about 95%, not a measured code or test percentage. The action-level parity audit is still open; this checkpoint does not declare the Qt port complete. Accessibility interaction testing and external writers that bypass the shared workspace lock remain follow-up work after feature parity.
