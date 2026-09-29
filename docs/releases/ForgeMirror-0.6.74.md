# ForgeMirror Qt 0.6.74 release verification

- Canonical version: root `VERSION` = `0.6.74`.
- Scope: stage 229 restores refreshable Pomodoro sound choices and bundled sound discovery. The Qt package now carries the three product sound files under `data/music`; a workspace-local `music` folder still takes precedence.
- Windows per-user installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.74.exe`; 12,480,558 bytes; FileVersion/ProductVersion `0.6.74`; SHA-256 `94FE3AC29D2ECAD512AD59C936F6FA00E5638D0CBAA91BD9C36927FCA58C8168`.
- Packaged Qt executable: `Z:\CPP\ForgeMirror\package-qt-0.6.74-release\ForgeMirrorQt.exe`; 3,432,960 bytes; FileVersion/ProductVersion `0.6.74`; SHA-256 `50B93A52B5A8312A649432792777145BAFFED86570CC89BCECC20608E6C1801C`.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.74-release` passed. CTest `smoke_qt` passed 1/1; `smoke_core` reported `OK`. Pomodoro coverage verifies live sound-list refresh, current-choice preservation, safe extension/path filtering and discovery of `data/music` when no workspace sound folder exists.
- The installed 0.6.74 executable opened and exited with code 0 using only Windows system directories in `PATH`; Qt plugin override variables were cleared. It saved `build-qt\lifecycle-0.6.74-251dc0ffa4984295a9f9670cb7279373\installed-smoke-0.6.74.png` (52,862 bytes). The same isolated startup check passed for the previous 0.6.73 package.
- The actual Inno Setup 6.7.3 installer compiled successfully. Lifecycle testing used a temporary AppId `66A1E761-175C-4B54-B057-F13F085616C0` and install path `C:\Users\mrdem\AppData\Local\Programs\ForgeMirrorQtLifecycle-251dc0ffa4984295a9f9670cb7279373`; the production AppId and installation were not used.
- Isolated 0.6.73 installation exited 0 and reported version 0.6.73. Updating over it with 0.6.74 exited 0, reported version 0.6.74, included all three `data/music` assets and passed packaged startup. Uninstall exited 0; the test EXE, test directory and temporary uninstall registry entry were removed.
- An external workspace marker remained unchanged across update and uninstall: SHA-256 `95755ECF7A6ABC11B9A64141A1733062F6AC10C538AE065A0124B687AFF15740`.
- `git diff --check` passed before commit. No running user application was stopped.

Functional migration remains an expert estimate of about 90%, not a measured code or test percentage. The action-level audit continues; accessibility interaction testing and older/external writers that bypass the shared lock remain follow-up quality work after feature parity.
