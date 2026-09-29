# ForgeMirror Qt 0.6.78 release verification

- Canonical version: root `VERSION` = `0.6.78`.
- Scope: stage 233 restores the administrator's explicit profile-rank assignment control in Qt.
- Windows per-user installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.78.exe`; 12,498,925 bytes; FileVersion/ProductVersion `0.6.78`; SHA-256 `2998D59283065905F4265A2EB3C57D36165DECE785C026AA45ED26DD13B915F4`.
- Packaged Qt executable: `Z:\CPP\ForgeMirror\package-qt-0.6.78-release\ForgeMirrorQt.exe`; 3,491,840 bytes; FileVersion/ProductVersion `0.6.78`; `--version` output `ForgeMirrorQt 0.6.78`; SHA-256 `904F6DBBF02B27D4390A9E5FF7678940E1E769866A444825ABD67E4A51ABD3FC`.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.78-release` passed. CTest `smoke_qt` passed 1/1 in 27.84 seconds; `smoke_core` reported `OK`. `smoke_qt` verifies that unauthenticated users cannot see the rank controls, that the selector follows the current profile rank, and that applying **Джуниор I** persists level and progress at 10. The fixture restores the original profile level and progress before later statistics checks.
- The packaged EXE's informational `--version` command exited 0 and printed `ForgeMirrorQt 0.6.78` with Qt plugin override variables cleared and only Windows system directories in `PATH`.
- The actual Inno Setup 6.7.3 installer compiled successfully; its FileVersion and ProductVersion both report `0.6.78`.
- Isolated lifecycle testing used temporary AppId `{4B347337-8898-42CD-AAE7-CCD579852939}` and per-user install path `C:\Users\mrdem\AppData\Local\Programs\ForgeMirrorQtLifecycle-14e4f2199dbe40738939497b48611eb6`; the production AppId and installation were not used. Version `0.6.77` installed, then `0.6.78` updated over it. Both uninstall entries and installed EXEs reported their expected versions; both startup smoke tests exited 0 with Qt removed from `PATH`. Screenshots: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.78-14e4f2199dbe40738939497b48611eb6\installed-0.6.77.png` (51,458 bytes) and `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.78-14e4f2199dbe40738939497b48611eb6\installed-0.6.78.png` (51,356 bytes).
- Uninstall exited 0, removed the isolated application files and uninstall entry, and preserved the external user-data marker (`4A71ED90ED064ABD58287A88FD93B4F980B61B27FC399AF32FE9A64F3E30EC34`) and workspace marker (`658F4617A670D04F955928EAD9139785523FE238E109533EA988F8E1511AFE44`) byte-for-byte.
- `git diff --check` passed before commit. The user-edited `AgentsSkills/CONTINUITY.md` remains separate from this release.

Functional migration remains an expert estimate of about 94%, not a measured code or test percentage. The action-level parity audit is still open; this checkpoint does not declare the Qt port complete. Accessibility interaction testing and external writers that bypass the shared workspace lock remain follow-up work after feature parity.
