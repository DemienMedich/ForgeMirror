# ForgeMirror Qt 0.6.90 release verification

Canonical version: root `VERSION` = `0.6.90`. The application, EXE and installer FileVersion/ProductVersion agree. This is the next verified UI checkpoint; the entire visual plan is not complete.

## Scope

- Responsive one-row header: secondary commands reuse the original menus through an explicit overflow; native navigation labels wrap without an icon-only rail. Large text no longer requires an outer horizontal scroll in the tested page matrix.
- Profile modes and cards use adaptive rows. Timer and achievement dimensions scale once from canonical base sizes. Project and pipeline columns prioritize names; complete context remains in details/tooltips/accessibility.
- Display settings use a scrollable form with a persistent Save/Cancel footer, short labels, separate geometry/preset disclosure and adaptive preset controls. Existing keys, presets and Save/Cancel semantics remain covered.
- Pipeline map opens on the selected stage, uses font-derived node/lane geometry, keyboard/route selection, palette-based zoom/reset icons, bounded 5–300% zoom and collapsed context. A real modal resize keeps the selected node visible without cancelling manual pan outside resize. The table follows snapshot order used by Move Up/Down instead of a second hidden branch ordering. Core normalization and domain mutations are unchanged.
- Pipeline-editor validation returns to the required title field while preserving the draft. Disclosure animates only its chevron (180 ms opening, 150 ms closing); state/accessibility are immediate, reversal starts at the current angle, and preference/system policy, hide and disable stop motion. Palette and compact base geometry are unchanged.

## Artifacts

| Artifact | Exact path | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| Current-user Windows installer | `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.90.exe` | 12,602,695 | `BF8CB45B6B12C1BE22A1374FE2BB6EFECEF6A27987A1A6DA0D8312D75FA8455E` |
| Packaged executable | `Z:\CPP\ForgeMirror\package-qt-0.6.90-release\ForgeMirrorQt.exe` | 3,856,896 | `388DF83778C26094F84D447FBC64E0382E4C98EB86D1ABA26F50ECDE5102903A` |
| Windows platform plugin | `Z:\CPP\ForgeMirror\package-qt-0.6.90-release\platforms\qwindows.dll` | 907,912 | `12577A7C4F2230BBEA53F279573D7E3D1A08D631197855481E1E1580F61BB877` |

The platform plugin's Qt version is `6.8.3.0`. The package was created in a fresh directory, deployed with the project script/windeployqt and app-local MSVC CRT. Its 31 files contain Qt/CRT, `qt.conf`, application and project music assets only; no profiles, JSON, INI, database, secret, test executable or debug artifacts were included. Inventory and metadata proof: `build-qt/package-0.6.90-inventory.json`, `build-qt/package-0.6.90-proof.json`.

## Verification

- Release targets `ForgeMirrorQt`, `smoke_qt` and `smoke_core` built with Qt 6.8.3/MSVC x64. After the final source changes the complete Qt suite passed 1/1 in 39.99 s (40.02 s total), with 37 accessible dialogs audited while captures were enabled. The packaging script repeated it successfully in 37.88 s (37.89 s total), with 35 visible-dialog audits without captures, and reported `smoke_core: OK`.
- Native Windows matrix: all 18 actual pages, including explicitly enabled optional 3D, at 800×520 and scales 90/100/110/125/150/175/200. All passed. Assertions cover page selection, outer horizontal range zero, Projects/Catalog/Pipeline no-match/reset states, original header menu identity, opening the actual overflow button, and restored wide header at 1120×720/100%. This is geometry/state coverage, not manual approval of every workflow/data set.
- Native focused filters passed for settings, map, disclosure and pipeline editor. Settings stayed 640×520 at 90/100/150/200%, collapsed/expanded geometry; preset controls/hint stayed within containers and Save/Cancel within screen/dialog bounds. Map passed at 1040×740 and 720×520/100%, and 720×520/200%; selected full bounds, 18/36 px icons, keyboard selection, zoom limits/reset, complete metadata and stable manual pan were asserted.
- Disclosure progressed in the real event loop, reversed without geometry/size-hint changes, survived rapid toggles and snapped when its policy was disabled or the control hidden/disabled. Tests read the Windows animation preference and verify the policy; the actual system setting was not toggled. Native focus and draft-preserving validation passed. Existing fresh-process administrator/session, help/version and persistence regressions remain passing.
- The map regression's monitor-containment assertion is native-only: the offscreen virtual monitor is 800 px wide and cannot establish monitor containment for a 1040 px dialog. Requested dimensions and control behavior remain tested offscreen; actual frame containment was verified on Windows.
- Package normal startup, GUI help and `--version` all exited 0 with `PATH=C:\Windows\System32;C:\Windows` and Qt/QML plugin variables unset. Normal startup displayed the filled profile, not Usage; GUI help uses the wide scrollable dialog. No stderr warnings/errors occurred. Runtime proof: `build-qt/package-0.6.90-runtime.json`; [profile capture](../design/visual-audit/2026-09-30/profile-packaged-after-0.6.90.png) SHA-256 `4A837C2A1D2D2A13E80BAB1556F7050E65AFAE600F8B54493B4341EB72C2779E`; [help capture](../design/visual-audit/2026-09-30/help-packaged-after-0.6.90.png) SHA-256 `7CDC3CF9FF471ABFD771256418740E0D5D1F38E166A0FA6E7771949207DB59F7`.

## Installer lifecycle

Inno Setup 6.7.3 built the real installer above with the permanent Qt AppId. `installer/verify-qt-lifecycle.ps1 -PreviousVersion 0.6.89 -CurrentVersion 0.6.90` then built equivalent test installers with a disposable AppId/directory and passed install → update → uninstall. It did not update the user's installation.

- Both installed EXEs started without development paths. Window titles contained `ForgeMirror · Qt migration · 0.6.89` / `0.6.90`; EXE versions and HKCU uninstall DisplayVersion matched. Current CLI printed `ForgeMirrorQt 0.6.90`.
- Installed `qwindows.dll` existed and matched each package SHA-256.
- Uninstall and its deadline-schedule helper both exited 0. Application directory and uninstall registration were removed.
- The external workspace marker was byte-identical after uninstall: SHA-256 `7BB6463B30F9E301FED333CDF8960CA9497B602CCD8EEB46AE42693FDEA15A4D`. No opt-in reminder task was created; this proves helper startup/no-existing-task behavior, not registration/removal of a real task.
- Evidence: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.90-57C1ACBA7B43491193FE172DE5D52F8A`; result: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.90-result.json`. Test-installer hashes differ from the real deliverable because of the disposable AppId/name.

Production data were not used. The user's installed EXE still reports `0.6.87`; its desktop shortcut was not changed. ImGui baseline `7306152` / `0.5.54` and the local, unstaged `AgentsSkills/CONTINUITY.md` ledger remain preserved. No PharosHub manifest was published.

## Remaining visual work

See the [scale/forms/map audit](../design/visual-audit/2026-09-30/UI_SCALE_FORMS_AND_PIPELINE_AUDIT.md). Other editor/form states, long/empty/first-run data, statistics variants, action/icon consistency and frequent workflows still need visual review. The 18×7 matrix does not cover every dialog, monitor/DPI or state. The full visual plan remains active.
