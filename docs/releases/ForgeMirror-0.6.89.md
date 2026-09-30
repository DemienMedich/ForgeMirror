# ForgeMirror Qt 0.6.89 release verification

- Canonical version at build: root `VERSION` = `0.6.89`; application, EXE and installer metadata agree.
- Scope: compact shell/control states, Profile Overview hierarchy, shared vector action icons, responsive work lists/KPI and 3D settings, readable GUI help, relative-workspace startup, and clean-PATH deadline helper resolution. The optional 180 ms navigation marker and reduced-motion policy remain enabled by preference. This is a verified UI checkpoint, not completion of the entire visual plan.

## Artifacts

| Artifact | Exact path | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| Current-user Windows installer | `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.89.exe` | 12,580,843 | `0912896EB42A83A08B16267B0F738C8E021F2AE5B5F2230C03224EAE705AF757` |
| Packaged executable | `Z:\CPP\ForgeMirror\package-qt-0.6.89-release\ForgeMirrorQt.exe` | 3,782,144 | `3862E7907DD38D56584E27F0E92AD08E1CA5B47345270F6B372B02594E28D2D3` |
| Windows platform plugin | `Z:\CPP\ForgeMirror\package-qt-0.6.89-release\platforms\qwindows.dll` | 907,912 | `12577A7C4F2230BBEA53F279573D7E3D1A08D631197855481E1E1580F61BB877` |

Installer and application FileVersion/ProductVersion are `0.6.89`. The plugin's own Qt version is `6.8.3.0`.

## Verification

- Release build of `ForgeMirrorQt`, `smoke_qt`, and `smoke_core` passed with Qt 6.8.3/MSVC x64. Final `ctest --test-dir build-qt -C Release --output-on-failure -R '^smoke_qt$'` passed 1/1 in 39.15 seconds (39.18 seconds total). `smoke_core` reported `OK`; Qt audited 27 accessible dialogs.
- Added checks cover compact Profile Overview/diagnostic expansion, empty tasks, original task titles and context, readable deadlines, preserved double-click task navigation, project table/details, unmatched filters/reset, and distinct checked-control rendering. Existing cross-process administrator-login, CLI help/version, relative-path startup and feature tests still pass.
- The native Windows all-18-page matrix passed at 800×520, including Projects/Catalog/Pipeline no-match states. The optional 3D module is now enabled in the fixture and both item visibility and actual selected page are asserted; the earlier loop fell back to Profile for those hidden pages. This exposed and fixed genuine 3D-settings overflow. Its narrow and wide (1120×720) native captures were inspected, and a regression verifies width usage using measured control sizes; see the [visual audit](../design/visual-audit/2026-09-30/UI_PROFILE_AND_WORK_LISTS_AUDIT.md).
- The package was created in a new directory using the project's deployment options, `windeployqt`, app-local MSVC CRT, `qt.conf`, and music assets only. Its 31 files were inventoried; there are no profiles, JSON, INI, databases, secrets or test executables. Inventory: `Z:\CPP\ForgeMirror\build-qt\package-0.6.89-inventory.json`.
- Packaged startup exited 0 with only Windows directories in `PATH` and Qt/QML plugin environment variables unset. Filled-workspace capture: `Z:\CPP\ForgeMirror\docs\design\visual-audit\2026-09-30\profile-hierarchy-after-1120.png`, SHA-256 `E069CECAE6F58C87FBDB8A6DB3D61016D69B81A7F9F327AC10CE718577488DCA`.
- Packaged native GUI help also exited 0 under the same clean environment. The 760×520 scrollable help was captured and inspected: [current help](../design/visual-audit/2026-09-30/command-help-after-760.png). CLI output remains covered separately by the process regression; help does not open workspace data.
- Inno Setup 6.7.3 built the actual current-user installer listed above with the permanent Qt AppId. `installer/verify-qt-lifecycle.ps1 -PreviousVersion 0.6.88 -CurrentVersion 0.6.89` then installed, updated and uninstalled equivalent test installers with a disposable AppId/directory, leaving the user's installed app untouched.
- Both installed versions launched with `PATH=C:\Windows\System32;C:\Windows` and Qt/QML variables unset. Window titles reported `ForgeMirror · Qt migration · 0.6.88` and `ForgeMirror · Qt migration · 0.6.89`; EXE versions and HKCU uninstall DisplayVersion matched. Current `--version` printed `ForgeMirrorQt 0.6.89`. The installed `qwindows.dll` was explicitly checked against each package SHA-256.
- Isolated uninstall and its deadline-schedule helper both exited 0, and the directory/registry record were removed. External workspace marker stayed byte-identical, SHA-256 `7BB6463B30F9E301FED333CDF8960CA9497B602CCD8EEB46AE42693FDEA15A4D`. Evidence: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.89-7C1B3D34D02E43E698B52566A7468442`; result JSON: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.89-result.json`.
- Production data were not used. The user's desktop shortcut still targets installed `0.6.87` without help arguments; this pass did not install over it. ImGui baseline remains `7306152` / `0.5.54`. No PharosHub manifest was published in this pass.

## Runtime issue found during release audit

The first clean-PATH lifecycle removed the application successfully, but its deadline-schedule helper returned 1. No reminder task existed. A read-only CreateProcess reproduction with `PATH=C:\Windows\System32;C:\Windows` failed to start `powershell.exe` by name (WinError 2), while the absolute executable under the directory returned by `GetSystemDirectoryW` ran `exit 0` successfully. The helper now resolves that system path; its existing foreign-task identity guard is unchanged. `TestQtDeadlineEvaluation` launches only a hidden no-op PowerShell process with the minimal PATH, never a scheduler mutation. The lifecycle verifier separately asserts the uninstall helper exit code instead of accepting only the outer uninstaller's success.

The final lifecycle passed that assertion with helper exit 0. No opt-in task was created for this audit, so this proves clean-environment helper startup and the no-existing-task path, not registration/removal of a real reminder task. User scheduler settings were not changed.

## Remaining visual work

The full visual plan remains open: long/empty/first-run states and scaling 90–200% across the entire matrix, remaining statistics views, forms/actions, pipeline-map context, and further bounded motion after static-state validation. The 800×520 matrix is not evidence that every scaled or empty state has been visually reviewed.
