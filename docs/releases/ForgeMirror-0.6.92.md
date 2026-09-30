# ForgeMirror Qt 0.6.92 release verification

Canonical version: root `VERSION` = `0.6.92`. Application, EXE and installer metadata agree. This is a catalog/profile UI checkpoint, **not completion of the visual plan**. Functional migration remains stage 269; no new migration stage is claimed.

## Scope

- Skill and profession editors use scrollable fields, wrapped labels, full link context and a persistent Save/Cancel footer. Required-field feedback focuses the field without clearing the draft. Existing domain/storage handlers and missing profession bindings remain intact.
- Profile management prioritizes search and its six-column list. Additional filters and sensitive output are disclosed; secondary archive/reset/delete commands retain their existing handlers and permission checks in a menu. The table has space independent of the controls scroll area. At 200%, all three fixture rows remain visible; oversized controls scroll rather than claiming to fit simultaneously.
- Profile editing/password forms use a single primary/default completion command, explicit reset confirmation, masked output and local keyboard handling. Creating a profile retains QInputDialog compatibility and leaves invalid input open with focus and feedback. Cancel is not autoDefault; focused Return/Enter invokes its local command without submitting another form.
- Task titles no longer repeat project context. Search retains canonical composed title data, IDs and navigation; full tooltip/accessibility context includes all assignees while the visible row stays short. Shared action icons scale from an immutable base; real cloud refresh does not reset the scaled size.
- Palette, compact grid, storage formats and existing optional disclosure motion are unchanged. This checkpoint adds no geometry animation.

## Artifacts

| Artifact | Exact path | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| Current-user Windows installer | `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.92.exe` | 12,622,279 | `4F5DB532D6680C49DF5E71941522805D53CC15B7D8FD54DFD931A3C5B01B2DF0` |
| Packaged executable | `Z:\CPP\ForgeMirror\package-qt-0.6.92-release\ForgeMirrorQt.exe` | 3,921,408 | `708A540B30689C40C9895A291B549F644B776E14A7F54D7B66E9E1AC940C3B70` |
| Windows platform plugin | `Z:\CPP\ForgeMirror\package-qt-0.6.92-release\platforms\qwindows.dll` | 907,912 | `12577A7C4F2230BBEA53F279573D7E3D1A08D631197855481E1E1580F61BB877` |

The fresh package has 31 inventoried application/Qt/CRT, `qt.conf` and project music files, with no profiles, JSON, INI, databases, secrets, test EXEs or debug artifacts. App-local MSVC CRT is supplied by the project. `qwindows.dll` metadata is Qt `6.8.3.0`. Evidence: `build-qt/package-0.6.92-inventory.json`, `package-0.6.92-proof.json`. Deployment warned about optional DX compiler files and undetected Visual Studio; clean-PATH launch below passed.

Inno Setup 6.7.3 built the real installer. Its permanent Qt AppId remains `8B99E76B-4510-49D8-AE45-9DDF85EA21DC`, distinct from ImGui. Installation is per user with lowest privileges; ordinary uninstall preserves external user data.

## Verification

- Final Release Qt suite passed **1/1, 45.00 s total, 131 accessible dialogs audited**; core-smoke reports OK. Prior complete package-build suite also passed in 45.73 s. Evidence: `build-qt/Testing/Temporary/LastTest.log`, final capture directory `build-qt/full-suite-accepted-0.6.92`.
- Ordinary direct native Windows catalog/profile test passed **90/100/110/125/150/175/200%**, exit 0 / empty stderr. It covers new/edit catalog forms, long Russian labels/bindings, field validation/draft preservation, keyboard Cancel/Return, password reset confirmation, six-column manager/menu state, filters/no-match, nested editor/create and masked/revealed/copied fixture credentials. File-byte assertions check cancellation; the clipboard is restored. Logs/captures: `build-qt/native-catalog-accepted-0.6.92` and matching `.out`/`.err`. Actual rendered primary pixels and sole default/Cancel behavior are checked, not just widget properties.
- BEFORE uses preserved 0.6.91 UI/Core libraries, not current code with styles disabled. Skill height at 200% was 640×605; current scroll/footer stays 640×520. The manager now retains three fixture rows at 200%. [Audit and persistent native captures](../design/visual-audit/2026-09-30/UI_CATALOG_PROFILES_AUDIT.md).
- Early hidden-launch catalog probes failed initial exposure and produced a create-dialog geometry warning. These did not recur with the ordinary direct launch; they are not claimed as a newly fixed product bug. A separately reproduced modal test-driver hang was eliminated by moving deferred timer cleanup after the driver returns. Its internal cause is unproven; it is not called UAF or an application fix.
- Current native 18-page test passed at **100%**, exit 0 / empty stderr (`build-qt/native-pages-0.6.92.out` / `.err`), with actual optional 3D destinations enabled. No screenshots were saved by that repeat. **The current 18×7 repeat and separate 200% invocation were blocked by execution policy; neither is claimed as PASS.** Prior 0.6.91 page-scale evidence is historical.
- Exact icon sizes survive 100→200→200→100 and real cloud refresh; native keyboard focus changes pixels. **Native pointer-hover acceptance remains OPEN:** quickRefresh has underMouse=0 and no changed pixels despite matching cursor/hit/bounds. Cause is unproven; the assertion remains enabled. `build-qt/native-navigation-diagnostic-final-0.6.92.err` records the failing aggregate separately from the passing full suite.
- Fresh package normal/help/version probes exited 0 with `PATH=C:\Windows\System32;C:\Windows` and Qt/QML plugin variables unset; all stderr files are empty, CLI prints `ForgeMirrorQt 0.6.92`. Normal startup displayed a synthetic profile, not Usage. Help was explicitly requested and displayed the wide scrollable dialog. Runtime proof: `build-qt/package-0.6.92-runtime.json`; [profile](../design/visual-audit/2026-09-30/profile-packaged-after-0.6.92.png) SHA `87235A9C123B06097FD467CD9C96787BE829CC2B22906A2016918ADFBF93E111`; [help](../design/visual-audit/2026-09-30/help-packaged-after-0.6.92.png) SHA `7CDC3CF9FF471ABFD771256418740E0D5D1F38E166A0FA6E7771949207DB59F7`.

## Installer lifecycle

`installer/verify-qt-lifecycle.ps1 -PreviousVersion 0.6.91 -CurrentVersion 0.6.92` passed isolated install → update → uninstall using **equivalent test installers with a disposable AppId/directory**, not the byte-identical release Setup and not the user's real installation.

| Test installer | Bytes | SHA-256 |
| --- | ---: | --- |
| `build-qt/lifecycle-0.6.92-6702C2DDD1B7478FB177489E92B5973B/payload-0.6.91/ForgeMirrorSetup_0.6.91.exe` | 12,609,592 | `27763EAFE202A41EAB90086C6C028996DEACB75422D5C899D15BDE68FE520B8E` |
| `build-qt/lifecycle-0.6.92-6702C2DDD1B7478FB177489E92B5973B/payload-0.6.92/ForgeMirrorSetup_0.6.92.exe` | 12,622,358 | `F37C8BED6306A9D9B5E1C06B3C36623B7591701464FAC7B86F03E12FFCB127A5` |

- Installed EXEs started without development paths. ProductVersion, window title and HKCU uninstall DisplayVersion agree for 0.6.91 and 0.6.92; current CLI reports 0.6.92. Installed qwindows.dll matches the packaged hash.
- Uninstaller and deadline-schedule helper exited 0; test install directory and uninstall registration were removed. No scheduler task was created or changed; this verifies helper startup/no-existing-task behavior, not a real reminder registration lifecycle.
- External workspace marker remained byte-identical: SHA `7BB6463B30F9E301FED333CDF8960CA9497B602CCD8EEB46AE42693FDEA15A4D`.
- Structured result: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.92-result.json`; evidence directory: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.92-6702C2DDD1B7478FB177489E92B5973B`.

Production data were not used. User installation/shortcuts remain **0.6.87**. ImGui baseline `7306152c603ff8007200f64e63c4188510d55588` / `0.5.54` and the intentionally unstaged `AgentsSkills/CONTINUITY.md` ledger are preserved. No PharosHub manifest was published.

## Remaining visual work

The [finite acceptance matrix](../design/UI_ACCEPTANCE_MATRIX.md) keeps chart labels at 200%, completion/manual-XP, service/cloud forms, remaining page states and timed motion/interruption/policy acceptance open. Native hover and the blocked current page-scale repeat are explicit limits. This package is a tested checkpoint; the entire redesign is not declared ready.
