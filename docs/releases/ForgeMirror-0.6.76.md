# ForgeMirror Qt 0.6.76 release verification

- Canonical version: root `VERSION` = `0.6.76`.
- Scope: stage 231 restores the profile overview's **Зоны перекоса** card: the three weakest categories in stable score order, each with its score and progress bar.
- Windows per-user installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.76.exe`; 12,493,655 bytes; FileVersion/ProductVersion `0.6.76`; SHA-256 `2A77FF12332CF267A5C8DB4F3AF39F330BD2595FC612D20EF986BD9BA221015E`.
- Packaged Qt executable: `Z:\CPP\ForgeMirror\package-qt-0.6.76-release\ForgeMirrorQt.exe`; 3,475,968 bytes; FileVersion/ProductVersion `0.6.76`; CLI `--version` output `ForgeMirrorQt 0.6.76`; SHA-256 `8E769101B30AF22FE13BB229BD3D7AA1ACC186108FE72E4302C1ABA6579EB3A5`.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.76-release` passed. CTest `smoke_qt` passed 1/1 in 28.03 seconds; `smoke_core` reported `OK`. `smoke_qt` uses a non-sorted category fixture and verifies the top three scores, labels and progress values. The fixture is restored before the independent profile-average statistics checks.
- The packaged executable opened and exited with code 0 using only Windows system directories in `PATH` and cleared Qt plugin override variables. Screenshot: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.76-c0829ae8390a4fa1a4320e44b2aceb3c\package-smoke.png` (51,627 bytes).
- The actual Inno Setup 6.7.3 installer compiled successfully; its FileVersion/ProductVersion are `0.6.76`. Isolated lifecycle testing used temporary AppId `{1313B4D3-A7FE-4A04-AA5F-6EA179E3F94F}` and install path `C:\Users\mrdem\AppData\Local\Programs\ForgeMirrorQtLifecycle-c0829ae8390a4fa1a4320e44b2aceb3c`. The production AppId and installation were not used.
- Isolated 0.6.75 installed successfully and reported `0.6.75` in its uninstall entry and EXE metadata. Updating over it with 0.6.76 exited 0; the uninstall entry and installed EXE both reported `0.6.76`. Startup tests for both versions exited 0 with Qt removed from `PATH`. The installed 0.6.76 screenshot is `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.76-c0829ae8390a4fa1a4320e44b2aceb3c\installed-0.6.76.png` (51,565 bytes).
- Uninstall exited 0, removed the isolated application and uninstall entry, and preserved an external user-data marker and workspace marker byte-for-byte. Their SHA-256 values were `4A71ED90ED064ABD58287A88FD93B4F980B61B27FC399AF32FE9A64F3E30EC34` and `658F4617A670D04F955928EAD9139785523FE238E109533EA988F8E1511AFE44`.
- `git diff --check` passed before commit. The user-edited `AgentsSkills/CONTINUITY.md` remains separate from this release.

Functional migration remains an expert estimate of about 92%, not a measured code or test percentage. The action-level parity audit is still open; this release checkpoint does not declare the Qt port complete. Accessibility interaction testing and external writers that bypass the shared workspace lock remain quality follow-up after feature parity.
