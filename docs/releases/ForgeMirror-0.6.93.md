# ForgeMirror Qt 0.6.93 — charts and XP forms

Date: 2026-09-30. Branch: `codex/qt-gui`. Canonical version: root `VERSION`, propagated to GUI/CLI, EXE metadata, installer and artifact name. **Scoped UI checkpoint; the complete redesign remains open.** [Audit, BEFORE/AFTER and limits](../design/visual-audit/2026-09-30/UI_CHARTS_AND_XP_AUDIT.md), [finite acceptance matrix](../design/UI_ACCEPTANCE_MATRIX.md).

## Delivery

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.93.exe` | 12,644,080 | `14FA935EBE0931F511823543416F66894F4D7E8E07E7A369B2D39B723BF09A80` |
| `Z:\CPP\ForgeMirror\package-qt-0.6.93-release\ForgeMirrorQt.exe` | 3,991,040 | `C33F53DB39D910A4DBA406B6F142CF9548A8325BAE10BEAAEE2C3BAB115FDDFA` |
| `Z:\CPP\ForgeMirror\package-qt-0.6.93-release\platforms\qwindows.dll` | 907,912 | `12577A7C4F2230BBEA53F279573D7E3D1A08D631197855481E1E1580F61BB877` |

This is a real per-user Windows Setup, not only a portable directory. Permanent Qt AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; uninstall registration and update behavior are retained. EXE and Setup FileVersion/ProductVersion are **0.6.93**; platform plugin version is **6.8.3.0**.

Fresh package inventory: **31 files**, only runtime/EXE/qt.conf/music payload; no profiles, local databases, secrets, cloud settings, test EXE or debug symbols. Evidence: `build-qt/package-0.6.93-inventory.json`, `package-0.6.93-proof.json`, `setup-0.6.93-proof.json`.

## What changed

- Three painted chart families use one measured text/layout model for paint and HFW. Report captions, status counts, twelve-month trend and endpoint dates remain complete; marks decimate only when needed. Log title/caption/date regions no longer share a fixed clipped height. Profile category/XP values retain all digits; dense radar axes use indices with full legends, and a narrow top-skill row places its name above XP.
- Task completion and manual XP use one scrollable body with a permanent summary/footer and explicit primary command. Labels/commands wrap or flow; participant/skill tables reserve real cell-widget width and height, including after reorder. The first native attempt confirmed clipping of `100` and ratings; assertions remain enabled and the final pass verifies full values.
- Sole default, local Return/Cancel, multiline description and explicit keyboard XP commit are tested. Task/skill/profile IDs, preview rules, splits, filtering/sorting, permissions and transactional domain handlers are preserved. Cancellation verifies byte-identical persisted data.
- Palette/density and existing optional motion are preserved; chart/table/form geometry is not animated. This release does not claim to finish the remaining motion, icon-state or service-form acceptance.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.93-release` completed. Full Release Qt-suite **1/1, 47.89 s** (47.90 total), **147 accessible dialogs**; `smoke_core: OK`. Existing domain/persistence coverage remains. `build-qt/build-package-0.6.93-first.log`, `build-qt/Testing/Temporary/LastTest.log`.
- Native Windows console-driver charts: **224 states** = 7 scales × 2 widths × (6 Report + 4 Log + 6 Profile fixtures), scales 90/100/110/125/150/175/200%, widths 640/320. Independent glyph bounds/overlap, unbroken numbers/dates, plotted coordinates/data, actual HFW height, same-instance width/font reversal and axis controls pass. **72 real Report selector contexts** verify current-period counts and independent audit trend; this is not acceptance of every report table. Exit 0 / empty stderr: `build-qt/native-charts-accepted-0.6.93.out` / `.err`.
- Native XP forms: both dialogs on all seven scales, 640×520, three long-name profiles/skills. Initial/invalid/reordered controls, three visible table rows, full SpinBox bounds, footer/default, outer Hrange=0, Tab/Backtab/local Return, filtering/sorting by ID, Cancel file bytes and four 100/200% keyboard commits pass. Exit 0 / empty stderr: `build-qt/native-xp-accepted-0.6.93.out` / `.err`.
- Genuine BEFORE uses preserved **0.6.92 UI/Core static libraries**, not current code with styling disabled. Core SHA `021F9A8183690DE61779CAC00D96AEE91E90011EC6122C3A2DB009E4241110E4`, UI SHA `E354933D96AB097D553B8286CF27079E2103854A088F3F10BADB595D8216E8F6`. Retained in `build-qt/visual-xp-charts-baseline-0.6.92`. Persistent index: [28 PNG and hashes](../design/visual-audit/2026-09-30/UI_CHARTS_AND_XP_EVIDENCE.json).
- Windows-only PATH startup probes: `C:\Windows\System32;C:\Windows`, Qt/QML plugin variables unset. Normal/help/version exit 0, all stderr empty; CLI `ForgeMirrorQt 0.6.93`. Normal startup shows a synthetic profile, not Usage. Help is explicitly requested and opens the wide scrollable dialog. [Profile screenshot](../design/visual-audit/2026-09-30/profile-packaged-after-0.6.93.png) SHA `55C854289E3D77E9F8CDA508EDAA598CC30D37E6A965269131EF445AB3D7105C`; [help](../design/visual-audit/2026-09-30/help-packaged-after-0.6.93.png) SHA `7CDC3CF9FF471ABFD771256418740E0D5D1F38E166A0FA6E7771949207DB59F7`. `build-qt/package-0.6.93-runtime.json`.
- First package probe used direct PowerShell GUI invocation and read output before exit. Explicit process Wait fixed the QA driver; no product-startup source change is claimed. Package startup probes are not focus/exposure acceptance; native form/renderer checks use the ordinary console-driver launch separately.
- Deployment warned that dxcompiler/dxil and automatic Visual Studio discovery were unavailable. The script copied the declared MSVC CRT; clean-PATH Widgets/platform startup passed. This is not a new claim about untested optional Direct3D shader paths.

## Isolated installer lifecycle

`installer/verify-qt-lifecycle.ps1 -PreviousVersion 0.6.92 -CurrentVersion 0.6.93` passed install → update → uninstall using **equivalent test installers with a disposable AppId/directory**. These are not the byte-identical release Setup and do not update the user's installation.

| Test installer | Bytes | SHA-256 |
| --- | ---: | --- |
| `build-qt/lifecycle-0.6.93-AA4F6834EAD042DEA6AF93639F4F49E5/payload-0.6.92/ForgeMirrorSetup_0.6.92.exe` | 12,622,360 | `87A23A1DBEF46D41813CA748F528132D51D54C1344F7B7D0BEA136B3368C5E43` |
| `build-qt/lifecycle-0.6.93-AA4F6834EAD042DEA6AF93639F4F49E5/payload-0.6.93/ForgeMirrorSetup_0.6.93.exe` | 12,644,162 | `761E2DEE3CE4C755E41C30734039F3E2531718EC3D29628C072C0DEB7A2BF647` |

- Installed EXE, title and HKCU uninstall versions agree for .92 and .93; current installed CLI reports 0.6.93. Both start without development paths and have the matching qwindows plugin.
- Uninstaller and deadline-schedule helper exit 0; disposable install directory and uninstall registration removed. No scheduler task was created/changed; helper startup/no-existing-task behavior is verified, not reminder registration lifecycle.
- External workspace marker unchanged: SHA `7BB6463B30F9E301FED333CDF8960CA9497B602CCD8EEB46AE42693FDEA15A4D`.
- Original evidence: `build-qt/verify-lifecycle-0.6.93.log`, directory `build-qt/lifecycle-0.6.93-AA4F6834EAD042DEA6AF93639F4F49E5`. `build-qt/lifecycle-0.6.93-result.json` is a structured extraction of that completed log, explicitly marked `DerivedFromLog`.

## Remaining / preserved

Native hover acceptance remains OPEN from .92; its assertion was not weakened or reclassified. The .92 18×7 page repeat and separate 200% invocation were policy-blocked; this scoped pass does not bypass them. The .91 seven-scale page evidence is historical, not a current full-page acceptance. Other page states, all report/admin-statistics tables, transition/bulk-task dialogs, [seven service-form families](../design/UI_SERVICE_FORMS_INVENTORY.md) and timed motion/system-policy checks remain open.

User installed EXE remains **0.6.87**, SHA `79F5E19B4B6BB4BBAC77502A9963693D97C643BCF3C3667DE793460C5E6337D3`; production data and shortcuts are untouched. ImGui `develop` / `codex/pre-qt-2026-08-28` baseline `7306152c603ff8007200f64e63c4188510d55588` / `0.5.54` and intentionally unstaged `AgentsSkills/CONTINUITY.md` are preserved. No PharosHub manifest was published.
