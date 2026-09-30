# ForgeMirror Qt 0.6.95 — decision forms and navigation lifecycle

Date: 2026-09-30. Branch: `codex/qt-gui`. Canonical root `VERSION` supplies GUI/CLI, EXE, Setup metadata and artifact name. **Verified scoped checkpoint; the full visual redesign remains active.** [Detailed BEFORE/AFTER and limits](../design/visual-audit/2026-09-30/UI_DECISION_FORMS_AUDIT.md), [finite acceptance matrix](../design/UI_ACCEPTANCE_MATRIX.md).

## Delivery

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.95.exe` | 12,675,116 | `AFD449B77691848AE8194BA35C4774F9DECB4DC77426A1B393EF067FAFBDAAD6` |
| `Z:\CPP\ForgeMirror\package-qt-0.6.95-release\ForgeMirrorQt.exe` | 4,098,560 | `F289043BA32262C02CAEC2B24DD972DC8F44C3CB3164827D08C757D46081DD43` |
| `Z:\CPP\ForgeMirror\package-qt-0.6.95-release\platforms\qwindows.dll` | 907,912 | `12577A7C4F2230BBEA53F279573D7E3D1A08D631197855481E1E1580F61BB877` |

Real per-user Windows Setup; permanent Qt AppId `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}` unchanged. EXE/Setup FileVersion and ProductVersion **0.6.95**, qwindows **6.8.3.0**. Package inventory **31 files**; no developer profiles, databases, secrets, cloud settings, test executables or debug symbols. [Persistent delivery proofs](evidence/0.6.95/setup-0.6.95-proof.json), [payload inventory](evidence/0.6.95/package-0.6.95-inventory.json).

## Full context and readable text

| Before | After |
| --- | --- |
| Cloud/storage resolver windows grow beyond the requested size; paths, summaries and Restore commands clip | Scrollable bodies, permanent footers, measured wrapping and command cells, flowing secondary commands; seven Cloud tabs and all IDs remain |
| Long confirmations displace context or buttons | Readonly scrollable context, full source/target/backup text, at least three text lines and safe Cancel default |
| Wallet profile/currency clip and suffix consumes numeric space; confirmation omits the amount | Reserved wrapped-label heights, separate currency, intrinsic numeric width; confirmation includes operation, amount, profile, currency, before/after balance and reason |
| Wallet history and shortcut help have unreadable rows or late height growth | Full dates/numbers/context, measured row heights, explicit empty history; all 17 keyboard shortcuts in a complete HFW table |
| Quick menu clips mixed process statuses, especially after paint or hot scale changes | Full textual status and icon, one measured status column, bounded label elision with complete name/path/status tooltip, explicit empty guidance; real Qt rendering retained |

## Commands and feedback

| Before | After |
| --- | --- |
| Shortcut create form mixes validation/actions with content; browse commands compress | Scrollable body and permanent error/Add/Cancel footer, measured flow row, required-field focus and preserved draft |
| Keyboard/default routes and picker context are implicit | Named controls, explicit Tab order, focused local Return/Cancel, one primary; original Qt file/folder picker modes with accessible location/type combos |

Domain permissions, IDs, transaction/recovery, numeric range/decimals, actual shortcut launch callbacks and palette remain. Internal tables may scroll when intrinsic columns exceed the viewport; outer form horizontal overflow is not accepted. At 200%, scrollable content is not promised to fit simultaneously.

## Interruptible motion

| Before | After |
| --- | --- |
| Navigation marker continues running after window hide/disable | Synchronous stop and selected-row snap on hide/disable, with per-frame policy checks; existing 180 ms OutCubic reversal stays continuous |

No table/page/form geometry animation was introduced; focus and shell layout remain unchanged. Destruction and an actual Windows SPI policy toggle are separate open gates.

## Verification

- Final fifth `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.95-release` exit **0**: Qt **1/1 PASS, 56.02 s** (56.18 total), **323 accessible dialogs**; `smoke_core: OK`. [Preserved full suite](evidence/0.6.95/qt-suite-accepted-0.6.95.log), [build/package log](evidence/0.6.95/build-package-0.6.95-fifth.log).
- Final ordinary console native driver, Qt Windows platform: **404 main-state checks / 83 modal contexts / 20 confirmations**, seven UI scales **90/100/110/125/150/175/200%**, requested 640×520 → 1000×640 → 640×520. Glyphs, intrinsic amounts including 1,000,000,000.00, overbalance, readonly/context, primary/default, focus/Tab/Return, draft and successful snapshot-read/Cancel byte equality pass. Actual credit/debit and cloud/storage confirmation callbacks run on fresh synthetic workspaces. **70 final decision PNG**, exit **0**, empty stderr.
- Final timed navigation probe: actual intermediate frame at 33 ms, reversal stop32→start32, hidden/disabled Stopped with correct endpoints, user-off immediate snap; two policy contexts, four PNG, exit0/stderr0. No forced animation clock or OS setting change.
- Actual InstantPopup quick-menu probe: four native contexts (100/200 × empty/populated), real running/stopped/unknown fixtures, full status/identity/icon/tooltip, actual row containment **after grab**, shared status width and Escape/byte preservation pass. Populated row widths **582/595 px**, within 640; no shortcut launched. Exit0/stderr0. Existing smoke additionally renders twice at each hot100→200→100→200 transition.
- Genuine BEFORE is preserved **0.6.94** UI/Core libraries, driver and sources; 24 native PNG100/200, exit0/stderr0. [Persistent UI evidence](../design/visual-audit/2026-09-30/UI_DECISION_FORMS_EVIDENCE.json) contains **120 PNG / 12 native logs / 4 prototype logs / 153 indexed artifacts**, hashes verified with zero mismatches. Eight prototype PNG are not native acceptance. Root actually reviewed seven final native images and two package images; an agent reviewed four additional final native images and two genuine BEFORE comparisons. Not all 70 final pictures were visually inspected.
- Initial sizing-only menu prototype passed hint checks but failed **after paint** at 722/896 px. Phase-gated geometry/paint fix passed separate hot-scale prototype579/599/579/599 before final product verification; both distinct prototype outcomes are retained. No capped/fake hint, shortened statuses or weakened bounds test.
- [Clean-PATH package runtime](evidence/0.6.95/package-0.6.95-runtime.json): Windows-only PATH, Qt/QML plugin variables unset; normal/help/version exit0, empty stderr, CLI `ForgeMirrorQt 0.6.95`. Normal startup shows the synthetic profile; explicit `--help` shows help. Hidden, waited startup probes are not native focus/exposure acceptance.
- Deployment retains optional dxcompiler/dxil and automatic Visual Studio discovery warnings. Declared CRT is copied and Widgets/platform startup passes; optional graphics paths are not accepted by these checks.

## Isolated installer lifecycle

Install **0.6.94 → update 0.6.95 → uninstall** passed using equivalent test installers with a **disposable AppId/directory**, not byte-identical release Setup and not an update of the user's installation. [Direct structured verifier output](evidence/0.6.95/lifecycle-0.6.95-result.json) has `DerivedFromLog=false`; evidence directory `build-qt/lifecycle-0.6.95-5271272C8C484EA5BB1995AD42D64D95`.

| Test Setup | Bytes | SHA-256 |
| --- | ---: | --- |
| `payload-0.6.94/ForgeMirrorSetup_0.6.94.exe` | 12,653,929 | `20496B00DE5D82B30F589443F10DAD3F8ECEF1EBECD7063BBC1245C861AFCA36` |
| `payload-0.6.95/ForgeMirrorSetup_0.6.95.exe` | 12,675,191 | `F6A6B725B998D8F5C7EDBDFDA7EF6C5C3001C341DE917EFFB974E46A5A234872` |

Installed EXE/actual window title/HKCU version match both versions, current CLI reports .95, platform plugin hashes match and clean-PATH startup exits0 with empty stderr. Uninstaller and deadline-schedule removal helper exit0; test directory/HKCU record removed. External marker SHA `7BB6463B30F9E301FED333CDF8960CA9497B602CCD8EEB46AE42693FDEA15A4D` unchanged. No scheduler task was created/changed.

### Background verification

| Before | After |
| --- | --- |
| Hidden startup returns empty `.NET Process.MainWindowTitle`, falsely failing the title gate | Read the actual Windows title only from HWNDs owned by the launched PID; keep launch hidden, require startup exit0/empty stderr |
| Automatic Git line-ending conversion changes archived diagnostic bytes and invalidates recorded hashes | Scoped archive attributes preserve raw bytes, including historical failure logs; 151 indexed archive Git blobs and separate package/delivery files match their raw inputs |

[Independent probe](evidence/0.6.95/hidden-window-title-probe.json) confirms .94 title while the .NET property is empty. Accepted verifier SHA `774A54747B4F638F28250B956599BB0F143AB847A9B8A2E8C9D59A3DDF23DDA8`. This corrects the QA helper; no unrelated application startup change was made.

## Preserved scope and remaining work

User installed **0.6.87** EXE SHA `79F5E19B4B6BB4BBAC77502A9963693D97C643BCF3C3667DE793460C5E6337D3`, production data, shortcuts, scheduler and Hub remain unchanged. ImGui `develop` and `codex/pre-qt-2026-08-28` remain `7306152c603ff8007200f64e63c4188510d55588` /0.5.54. `AgentsSkills/CONTINUITY.md` stays intentionally unstaged.

Open: other rule/vault/banner error variants; cloud preview/pull/update and unplayed confirmation callbacks; general history/export/import/diagnostics/destructive dialogs; pipeline transition/bulk edits; remaining page/table/icon states; native hover, destruction and actual system motion-policy acceptance. Policy-blocked 18×7 rerun is neither bypassed nor relabelled. This release does not complete the whole UI redesign.
