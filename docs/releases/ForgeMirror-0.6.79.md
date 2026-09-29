# ForgeMirror Qt 0.6.79 release verification

- Canonical version: root `VERSION` = `0.6.79`.
- Scope: stage 234 restores the administrator's legacy profile-report action to copy the reports folder path to the clipboard.
- Windows per-user installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.79.exe`; 12,497,836 bytes; FileVersion/ProductVersion `0.6.79`; SHA-256 `926C30E2F16D97F61B525218BFB383915784FEB37FF9255A2B871990F20A7A52`.
- Packaged Qt executable: `Z:\CPP\ForgeMirror\package-qt-0.6.79-release\ForgeMirrorQt.exe`; 3,493,888 bytes; FileVersion/ProductVersion `0.6.79`; `--version` output `ForgeMirrorQt 0.6.79`; SHA-256 `DFA1516F802D71B140E9C569D1A7EDDBC72097AAB6F18AEA7044FF75EA063B12`.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.79-release` passed. CTest `smoke_qt` passed 1/1 in 28.13 seconds; `smoke_core` reported `OK`. The new Qt smoke assertion triggers the profile-report path-copy action, checks the exact native-separator path in the clipboard, and checks the status-bar confirmation.
- The packaged EXE's informational `--version` command exited 0 and printed `ForgeMirrorQt 0.6.79` with Qt plugin override variables cleared and only Windows system directories in `PATH`.
- The actual Inno Setup 6.7.3 installer compiled successfully; its FileVersion and ProductVersion both report `0.6.79`.
- Isolated install lifecycle test used temporary AppId `{8B47498F-C855-4BB3-8479-7A7D822E54C5}` and per-user install path `C:\Users\mrdem\AppData\Local\Programs\ForgeMirrorQtLifecycle-b3904f143efb472cba84dfd8b75163ed`; production AppId and installation were not used. Version `0.6.78` installed, then `0.6.79` updated over it. Both uninstall entries and installed EXEs reported expected versions; both startup smoke tests exited 0 with Qt removed from `PATH`. Screenshots: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.79-b3904f143efb472cba84dfd8b75163ed\installed-0.6.78.png` (51,726 bytes) and `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.79-b3904f143efb472cba84dfd8b75163ed\installed-0.6.79.png` (51,799 bytes).
- Uninstall exited 0, removed the isolated application files and uninstall entry, and preserved the external user-data marker (`4A71ED90ED064ABD58287A88FD93B4F980B61B27FC399AF32FE9A64F3E30EC34`) and workspace marker (`658F4617A670D04F955928EAD9139785523FE238E109533EA988F8E1511AFE44`) byte-for-byte.
- `git diff --check` passed before commit. The user-edited `AgentsSkills/CONTINUITY.md` remains separate from this release.

Functional migration remains an expert estimate of about 95%, not a measured code or test percentage. The action-level parity audit is still open; this checkpoint does not declare the Qt port complete. Accessibility interaction testing and external writers that bypass the shared workspace lock remain follow-up work after feature parity.
