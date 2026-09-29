# ForgeMirror Qt 0.6.83 release verification

- Canonical version: root `VERSION` = `0.6.83`.
- Scope: stage 238 restores the selected task's complete read-only context: resolved project and participant names, skill list, current and next pipeline step, stage hints, priority, deadline, and awarded XP totals/breakdown. Legacy pipeline labels are resolved when a stable stage ID is missing.
- Windows per-user installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.83.exe`; 12,524,423 bytes; FileVersion/ProductVersion `0.6.83`; SHA-256 `8139EBC08DCC16D41146927303C14DC1FB0DF7501AD3C4FA71E15AE5CE1BD846`.
- Packaged Qt executable: `Z:\CPP\ForgeMirror\package-qt-0.6.83-release\ForgeMirrorQt.exe`; 3,582,976 bytes; FileVersion/ProductVersion `0.6.83`; `--version` output `ForgeMirrorQt 0.6.83`; SHA-256 `6F758A6BA1E5F91D80CA6F85DFDC2132FAD778C4378DC7D1EEAC81284833BEFB`.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.83-release` passed. CTest `smoke_qt` passed 1/1 in 28.97 seconds; `smoke_core` reported `OK`. The new Qt smoke checks resolved project/stage/participant context, skills, next-stage route, stage-hint overflow, and global/skill XP totals.
- Packaged `ForgeMirrorQt.exe --version` exited 0 and printed `ForgeMirrorQt 0.6.83` with Qt plugin override variables cleared and only Windows system directories in `PATH`. Packaged startup with `--smoke-test --screenshot ... --storage-dir <disposable workspace>` exited 0 under that restricted `PATH`. Screenshot: `Z:\CPP\ForgeMirror\build-qt\release-smoke-0.6.83\window.png` (51,257 bytes; SHA-256 `F7C29104C8118080FB2AD53CE9B3DC5F92600EF4D018BC7CB3015628F963666F`).
- The actual Inno Setup 6.7.3 installer compiled successfully; FileVersion and ProductVersion both report `0.6.83`.
- Isolated install lifecycle used temporary AppId `{418275F5-AC96-4B70-B6D9-416795DBAA05}` and per-user path `C:\Users\mrdem\AppData\Local\Programs\ForgeMirrorQtLifecycle-52c8c17d7fb1426da2054542b1ea1880`; production AppId and installation were not used. Version `0.6.82` installed, then `0.6.83` updated over it. The uninstall entry and installed EXE reported expected versions at both checkpoints; both startup smoke tests exited 0 with Qt removed from `PATH`. Screenshots: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.83-52c8c17d7fb1426da2054542b1ea1880\installed-0.6.82.png` (52,161 bytes; SHA-256 `CBF120FF70B5C544CAEB882DC939024614DB51502B0FE55C3D775A76AE273CB6`) and `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.83-52c8c17d7fb1426da2054542b1ea1880\installed-0.6.83.png` (52,109 bytes; SHA-256 `22CF16542BEA3BF83C8857027FB254E091C2B5F7A743FDD72317DAAD8AC22896`).
- Uninstall exited 0, removed the isolated application files and uninstall entry, and preserved external user-data marker `4A71ED90ED064ABD58287A88FD93B4F980B61B27FC399AF32FE9A64F3E30EC34` and workspace marker `658F4617A670D04F955928EAD9139785523FE238E109533EA988F8E1511AFE44` byte-for-byte.
- `git diff --check` passed before commit. The user-edited `AgentsSkills/CONTINUITY.md` remains separate from this release.

Functional migration remains an expert estimate of about 95%, not a measured code or test percentage. The action-level parity audit remains open; this checkpoint does not declare the Qt port complete. Accessibility interaction testing and external writers that bypass the shared workspace lock remain follow-up work after feature parity.
