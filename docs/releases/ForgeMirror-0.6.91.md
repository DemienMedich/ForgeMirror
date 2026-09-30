# ForgeMirror Qt 0.6.91 release verification

Canonical version: root `VERSION` = `0.6.91`. Application, EXE and installer versions agree. This is a verified working-editor UI checkpoint, **not completion of the entire visual plan**. The mapped functional migration remains stage 269; this release does not invent another migration stage.

## Scope

- Task/project creation and editing use a scrollable form and a persistent Save/Cancel footer. The task's XP, penalty and skill context is explicitly disclosed; values and awarded-task restrictions are preserved. Compact text/list heights, wrapping labels, full tooltips/accessibility and adaptive inline-project commands retain the existing palette.
- All four pipeline-editor tabs scroll independently; the footer remains outside the tabs. Short tab captions retain full tooltips. Long link text wraps and exposes complete accessible text. IDs, original link order, missing links, self-links, snapshots and transaction/rollback paths are unchanged.
- Validation focuses the required title and preserves drafts. Main Save is the sole styled primary/default action, registered after the button box has its dialog parent. Secondary buttons cannot replace it. Return/numpad Enter on a focused secondary button invokes that button; Enter within the inline project's single-line title saves that project without submitting the task. Multiline fields are not covered by this shortcut.
- A shared intrinsic height-for-width guard prevents a nested form from reporting a height smaller than its own natural content. Native tests reproduced and then eliminated clipping of the skills field at 110/125/150%. No height animation, fixed spacer workaround, storage schema or domain behavior was added.
- Existing optional disclosure motion animates its chevron only, with saved/system motion policy. Forms change state immediately; the main palette and compact base layout are unchanged.

## Artifacts

| Artifact | Exact path | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| Current-user Windows installer | `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.91.exe` | 12,609,520 | `A6170BF71720E4AA4D768E8060332DEC284F5E6FA743148734E09E0EF62884A1` |
| Packaged executable | `Z:\CPP\ForgeMirror\package-qt-0.6.91-release\ForgeMirrorQt.exe` | 3,876,864 | `45E4D4AEE2B73B908E01A71B9027902E6BFB39CBD6820B6B07C725B245C527DE` |
| Windows platform plugin | `Z:\CPP\ForgeMirror\package-qt-0.6.91-release\platforms\qwindows.dll` | 907,912 | `12577A7C4F2230BBEA53F279573D7E3D1A08D631197855481E1E1580F61BB877` |

The fresh package has 31 inventoried application/Qt/CRT, `qt.conf` and project music files. No profiles, JSON, INI, database, secret, test executable or debug artifacts are present. `qwindows.dll` reports Qt `6.8.3.0`; app-local MSVC CRT is included. Inventory/metadata: `build-qt/package-0.6.91-inventory.json`, `build-qt/package-0.6.91-proof.json`. windeployqt warned about absent optional DX compiler and undetected Visual Studio; project deployment supplies the CRT explicitly, and clean-PATH launch below passed.

## Verification

- Release targets `ForgeMirrorQt`, `smoke_qt`, `smoke_core` built with MSVC x64/Qt 6.8.3. Final complete Qt suite passed 1/1 in **41.74 s** (41.75 s total), with **61 accessible dialogs audited** while captures were enabled. `smoke_core: OK`. Evidence: `build-qt/Testing/Temporary/LastTest.log`.
- Native Windows working-editor matrix passed scales **90/100/110/125/150/175/200** at 640×520: task/project, all four pipeline tabs, required-title errors, additional/inline-project disclosure, reopening and resizing 640→800→640, internal control bounds, wrapped labels, single scroll/primary/default action and persistent footer. Space toggles links, selected missing/self links and drafts survive tab changes; focused Cancel Return/Enter leaves persisted files byte-identical. Logs/captures: `build-qt/native-working-editors-0.6.91` and matching `.out`/`.err`; native stderr is empty.
- Native settings passed 640×520 at 90/100/150/200%, collapsed/expanded geometry and focused Cancel Return. Native 18-page × seven-scale layout matrix and disclosure event-loop regression were rerun successfully. These are geometry/state checks, not manual approval of all workflows, DPI/monitors or user data.
- Actual native Return probes compare prebuilt 0.6.90 libraries against current UI libraries. Before, Enter in the inline project title submitted the outer task (0 projects/1 task); after, inline title and local Save both create 1 project/0 tasks and keep the task form open. Local Cancel creates neither and keeps the draft. Logs: `build-qt/visual-forms-baseline-0.6.90/enter-probe/final-inlineName.out`, `final-inlineSave.out`, `final-inlineCancel.out`. The existing inline-project persistence test also passed independently on native Windows and offscreen. It covers saving the task after local Save, preserving an already saved project when the task is cancelled, and local Cancel without submitting the task.
- Earlier full-suite attempts correctly failed new assertions: the offscreen monitor clamps an 800 px request to 780 px, and queued focus/layout updates had not settled. Tests now account for the helper's actual maximum size and process those events before inspecting geometry or dispatching keys. Footer/internal-bound assertions remain enabled; native paths were rerun. These test-environment adjustments are not described as application fixes.
- Package normal startup, GUI help and CLI `--version` exited 0 with `PATH=C:\Windows\System32;C:\Windows` and Qt/QML plugin variables unset; all three stderr files are empty. Normal launch displayed the synthetic profile, **not Usage**. Help remains explicitly requested and shows the wide scrollable parameter dialog. CLI printed `ForgeMirrorQt 0.6.91`. Runtime proof: `build-qt/package-0.6.91-runtime.json`; [normal profile](../design/visual-audit/2026-09-30/profile-packaged-after-0.6.91.png) SHA `80166025E7FCC3DF30C9150BA0A75A96098627D0672995D92BE97A07BAC014C2`; [explicit help](../design/visual-audit/2026-09-30/help-packaged-after-0.6.91.png) SHA `7CDC3CF9FF471ABFD771256418740E0D5D1F38E166A0FA6E7771949207DB59F7`.

## Installer lifecycle

Inno Setup 6.7.3 built the real installer with the permanent Qt AppId. `installer/verify-qt-lifecycle.ps1 -PreviousVersion 0.6.90 -CurrentVersion 0.6.91` passed install → update → uninstall using equivalent test installers with a disposable AppId/directory, without updating the user's installation.

- Installed EXEs started without development paths. ProductVersion, window title and HKCU uninstall DisplayVersion agreed for both versions; current CLI printed `ForgeMirrorQt 0.6.91`.
- Installed `platforms/qwindows.dll` existed and matched the packaged hash above.
- Uninstaller and deadline-schedule helper both exited 0. Test application directory and uninstall registration were removed.
- External workspace marker remained byte-identical: SHA-256 `7BB6463B30F9E301FED333CDF8960CA9497B602CCD8EEB46AE42693FDEA15A4D`. No opt-in reminder task was created: this checks helper startup/no-existing-task behavior, not registering/removing a real task.
- Evidence directory: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.91-FFDEBBFDD7AD47E2BF2181D2D8AF0F1F`; result: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.91-result.json`. The lifecycle was repeated to persist its structured result; both runs passed. Test-installer hashes differ from the deliverable due to the disposable identity.

Production data were not used. User installation/desktop shortcut remain **0.6.87**. ImGui baseline `7306152c603ff8007200f64e63c4188510d55588` / `0.5.54` and the intentionally unstaged local `AgentsSkills/CONTINUITY.md` ledger are preserved. No PharosHub manifest was published.

## Remaining visual work

See the [working-editor audit and before/after captures](../design/visual-audit/2026-09-30/UI_WORKING_EDITORS_AUDIT.md). This matrix does not cover all awarded-task/edit variants, tab traversal, profile/password/skill/profession/vault/rules/cloud forms, every monitor or production data. Those states, remaining action/icon consistency and further motion follow in the active UI plan.
