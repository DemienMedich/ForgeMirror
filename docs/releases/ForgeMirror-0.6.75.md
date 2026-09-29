# ForgeMirror Qt 0.6.75 release verification

- Canonical version: root `VERSION` = `0.6.75`.
- Scope: stage 230 restores the Qt profile overview's activity and balance summary, next milestone/recovery signal, assigned-task focus and workload, weakest category, urgent task preview, and the profile-scoped actions to open/filter tasks.
- Windows per-user installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.75.exe`; 12,488,789 bytes; FileVersion/ProductVersion `0.6.75`; SHA-256 `5C3BB58A7EB448D9F7F49A12958394A46A8ADC2751B26A1DD5219ABF4C5739D6`.
- Packaged Qt executable: `Z:\CPP\ForgeMirror\package-qt-0.6.75-release\ForgeMirrorQt.exe`; 3,462,656 bytes; FileVersion/ProductVersion `0.6.75`; CLI `--version` output `ForgeMirrorQt 0.6.75`; SHA-256 `163F2EA037F48BBAA2DA1E7C1E1415F954D3F71C00144A9E571344E5FA0EA408`.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.75-release` passed. CTest `smoke_qt` passed 1/1 in 27.52 seconds; `smoke_core` reported `OK`. The new UI test checks profile state/focus/load/weak-zone values, the active-task preview, and that opening the focus task selects its assigned row in the Tasks page.
- The packaged executable opened and exited with code 0 with only Windows system directories in `PATH` and Qt plugin override variables cleared. Screenshot: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.75-6f2894310a8f44299284f698724eb3ba\smoke.png` (49,853 bytes).
- The actual Inno Setup 6.7.3 installer compiled successfully. Isolated lifecycle testing used temporary AppId `{C7DA2657-742C-4684-9A15-B8988CC86696}` and install path `C:\Users\mrdem\AppData\Local\Programs\ForgeMirrorQtLifecycle-711fb775f77f45bd808a7f1daae6f511`; the production AppId and installation were not used.
- Isolated 0.6.74 installed successfully and reported `0.6.74` in its uninstall registration and EXE metadata. Updating over it with 0.6.75 exited 0 and both the uninstall registration and installed EXE reported `0.6.75`. Startup smoke tests for both installed versions exited 0 with Qt removed from `PATH`; the 0.6.75 screenshot is `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.75-711fb775f77f45bd808a7f1daae6f511\installed-0.6.75.png` (49,443 bytes).
- Uninstall exited 0, removed the isolated application and temporary uninstall entry, and preserved both an external user-data marker and the workspace marker byte-for-byte. Their SHA-256 values were `4A71ED90ED064ABD58287A88FD93B4F980B61B27FC399AF32FE9A64F3E30EC34` and `658F4617A670D04F955928EAD9139785523FE238E109533EA988F8E1511AFE44` respectively; the full marker files remain in the isolated test folder.
- `git diff --check` passed before commit. Existing production files and the separate user-edited `AgentsSkills/CONTINUITY.md` were not used or included in this release.

Functional migration remains an expert estimate of about 91%, not a measured code or test percentage. Qt covers all 18 legacy workspace tabs, but an action-level parity audit remains; this release checkpoint does not declare the migration complete. Accessibility interaction testing and writers that bypass the shared workspace lock remain quality follow-up after feature parity.
