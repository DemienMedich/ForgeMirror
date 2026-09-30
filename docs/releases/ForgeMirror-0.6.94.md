# ForgeMirror Qt 0.6.94 — service editor UI

Date: 2026-09-30. Branch: `codex/qt-gui`. Root `VERSION` is the canonical version for GUI/CLI, EXE metadata, Setup and artifact name. **Scoped checkpoint; the full UI redesign remains active.** [BEFORE/AFTER, diagnosis and exact limits](../design/visual-audit/2026-09-30/UI_SERVICE_EDITORS_AUDIT.md), [finite acceptance matrix](../design/UI_ACCEPTANCE_MATRIX.md).

## Delivery

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.94.exe` | 12,653,850 | `DDBBA5FA3DC87A0CB9DA6D9AC04E13B8741FD71CF50190F28C172AC89327D5AD` |
| `Z:\CPP\ForgeMirror\package-qt-0.6.94-release\ForgeMirrorQt.exe` | 4,030,464 | `0CBF8A505DA342E6C12BE7EDDE34DD4266C403756AD301DEC0D4DDB64077ACCC` |
| `Z:\CPP\ForgeMirror\package-qt-0.6.94-release\platforms\qwindows.dll` | 907,912 | `12577A7C4F2230BBEA53F279573D7E3D1A08D631197855481E1E1580F61BB877` |

Real per-user Windows Setup; permanent Qt AppId `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}` is unchanged. EXE/Setup FileVersion and ProductVersion are **0.6.94**; plugin version **6.8.3.0**. Fresh package inventory **31 files**, only EXE/runtime/qt.conf/music assets: no profiles, local databases, secrets, cloud settings, test executables or debug symbols. `build-qt/package-0.6.94-inventory.json`, `package-0.6.94-proof.json`, `setup-0.6.94-proof.json`.

## Text, layout and feedback

| Before | After |
| --- | --- |
| Rules/Vault/Banner/Cloud forms grow or clip content at high scale; commit/error controls are mixed with content | Scrollable bodies, permanent validation and Save/Cancel footers, semantic groups, wrapped labels and full accessible context |
| Preset commands and reward days compress into a single row | Measured FlowRow keeps fitting pairs together and wraps individual commands; hidden controls leave no gap |
| Rules/Vault numeric editors can be smaller than their full values/suffixes | Polished intrinsic widths are reserved; captions stack above fields when their measured combined width exceeds preferred form space |
| Banner text has a 110 px maximum | Font-aware four-line minimum, no fixed maximum, multiline Return and Tab advancing focus |
| Rules history has wide ISO timestamps/unwrapped changes or an orphan table on corrupt data | Two-line visible date/time, full ISO tooltip/accessibility, wrapped changes/measured rows, clear empty/corrupt states |
| Stacked Cloud root row gains large unused vertical stretch; checkbox captions clip | Intrinsic field/button row has measured compact gap; shorter captions retain complete visible warning and profile/interval explanations |
| Default/secondary commands and invalid-field feedback are implicit | One painted primary/default Save, explicit Tab chains and local focused Return/Cancel; invalid preset stays open, relevant required fields receive focus, drafts remain |
| Folder picker location/type controls lack accessible names | Existing Directory / ShowDirsOnly / DontUseNativeDialog selection remains, with named combos and regression checks |

Domain save/ID/security/stale/locked/rollback handlers remain. Palette/density and existing optional motion are preserved; no chart/table/form geometry animation was added.

## Verification

- Full `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.94-release` completed: Release Qt suite **1/1 PASS, 49.19 s** (49.34 total), **216 accessible dialogs**; `smoke_core: OK`. `build-qt/build-package-0.6.94-accepted.log`, preserved `build-qt/qt-suite-accepted-0.6.94.log`.
- Actual native Windows console driver: **245 main layout inspections** = 7 scales × 7 editor/source contexts × 5 initial/wide/reversed/draft inspections. Scales 90/100/110/125/150/175/200%, main requested size 640×520. Full glyphs, numeric/current editor values, wrapping, footer/bounds, outer Hrange=0, FlowRow measurement/hide/show, actual primary pixels, keyboard/focus/draft/Cancel and ten Escape byte-preserving cases pass. **Ten nested contexts** at 100/200% cover invalid preset names, empty/populated/corrupt history and actual Qt folder picker. Exit 0 / empty stderr: `build-qt/native-service-accepted-0.6.94.out/.err`.
- Nested folder picker keeps standard Qt text/dimensions; only screen bounds, mode/options, command readability/accessibility and Cancel preserving root/parent draft are accepted. Forced preset640×520 proves bounds/readability, not natural-size compactness. Pictures alone do not establish keyboard/persistence outcomes; console assertions do.
- Genuine BEFORE uses preserved .93 UI/Core libraries and source in `build-qt/visual-service-baseline-0.6.93`, not current code with a styling toggle. UI SHA `573C12BF3B9C2A2C8A3A7CEC0CB6DD5F3509D6F35DB48FCC20F0E11BF0015B6B`; Core SHA `A45CC5206549B689FBD5D20C311FBDC97C3701F38B77E4063F80DF0D5F9029F7`. Native BEFORE100/200 exit0/stderr0; 14 before PNG, 52 final after PNG and one intermediate Cloud capture are indexed in [persistent evidence](../design/visual-audit/2026-09-30/UI_SERVICE_EDITORS_EVIDENCE.json).
- Intermediate native review confirmed Cloud row304→124 px / gap76→16 px after stretch removal. Offscreen full and isolated runs confirmed actual/intrinsic width mismatch; local caption/field/layout fixes resolve it. Intermediate Ignored-label attempts failed HFW checks and were reverted. Later full-suite accessibility failures led to actual named picker controls. Assertions/timeouts remain strict; exact resolved fallback-font identity is not claimed.
- Windows-only PATH `C:\Windows\System32;C:\Windows`, Qt/QML plugin variables unset: normal/help/version exit0, stderr empty, CLI `ForgeMirrorQt 0.6.94`. [Normal synthetic profile](../design/visual-audit/2026-09-30/profile-packaged-after-0.6.94.png) SHA `C23157A73314EA0CD36B4C5A005553CE88C5E8EF03E6C53CDC617C97DEE9DDAF`, not Usage; [explicit help](../design/visual-audit/2026-09-30/help-packaged-after-0.6.94.png) SHA `7CDC3CF9FF471ABFD771256418740E0D5D1F38E166A0FA6E7771949207DB59F7`. `build-qt/package-0.6.94-runtime.json`. Startup probes use explicit process Wait and are not native exposure/focus acceptance.
- Deployment warned about missing optional dxcompiler/dxil and automatic Visual Studio discovery. Declared MSVC CRT is copied; Widgets/platform clean-PATH startup passes. No new optional Direct3D shader acceptance is claimed.

## Isolated installer lifecycle

`installer/verify-qt-lifecycle.ps1 -PreviousVersion 0.6.93 -CurrentVersion 0.6.94` passed install → update → uninstall using **equivalent test installers with a disposable AppId/directory**. These are not byte-identical release Setup binaries and do not update the user's installed program.

| Test Setup | Bytes | SHA-256 |
| --- | ---: | --- |
| `build-qt/lifecycle-0.6.94-CA0C8CE3C5524204BF10E9C22B0C1CFB/payload-0.6.93/ForgeMirrorSetup_0.6.93.exe` | 12,644,163 | `55539019B1057507114970283C2180BFE2B09448B0E01B2088F887CFD0FFC414` |
| `build-qt/lifecycle-0.6.94-CA0C8CE3C5524204BF10E9C22B0C1CFB/payload-0.6.94/ForgeMirrorSetup_0.6.94.exe` | 12,653,935 | `3C01D30FB52AF904BD5BD36BCEFB6B4B59281845CC603525006591BD4FEF4C0A` |

- Installed EXE/title/HKCU uninstall versions match .93/.94, current installed CLI reports .94. Both start without development paths and have the matching qwindows plugin.
- Uninstaller and deadline-schedule helper exit0; test install directory/HKCU registration removed; external workspace marker SHA `7BB6463B30F9E301FED333CDF8960CA9497B602CCD8EEB46AE42693FDEA15A4D` unchanged. No scheduler task was created/changed; this is not reminder registration lifecycle acceptance.
- Original log `build-qt/verify-lifecycle-0.6.94.log`; structured `build-qt/lifecycle-0.6.94-result.json` is explicitly **DerivedFromLog**.

## Screenshot origin and preserved scope

The user's Usage screenshot is the earlier explicit QA `--help` launch of temporary **0.6.88**, not a crash or normal-startup defect; exact path/session proof is in the audit. Desktop/StartMenu Qt shortcuts have empty arguments and target installed **0.6.87**. User EXE SHA `79F5E19B4B6BB4BBAC77502A9963693D97C643BCF3C3667DE793460C5E6337D3`; installation, shortcuts and production data remain untouched.

ImGui `develop` and `codex/pre-qt-2026-08-28` remain `7306152c603ff8007200f64e63c4188510d55588` /0.5.54. `AgentsSkills/CONTINUITY.md` stays intentionally unstaged. No PharosHub manifest was published. Wallet movements, shortcut forms, cloud preview/pull/update decisions, conflict resolvers, general confirmation/export/import, pipeline transition/bulk edit, remaining page/table states and timed motion/system-policy acceptance remain open. Native hover OPEN and policy-blocked18×7 rerun are neither bypassed nor reclassified by this scoped checkpoint.
