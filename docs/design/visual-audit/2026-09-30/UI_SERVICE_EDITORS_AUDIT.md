# Service editors — bounded UI audit 0.6.94

Date: 2026-09-30. Branch: `codex/qt-gui`. **Scoped native and full-suite PASS; installer lifecycle is recorded in the release note.** Scope is four existing editors (Rules, Vault settings, Banner create/edit, CloudSettings), nested Rules preset/history and the existing Qt folder picker. This is only part of [the seven-family inventory](../../UI_SERVICE_FORMS_INVENTORY.md); the complete UI redesign remains open.

## Genuine BEFORE

Preserved .93 UI/Core libraries and Qt source files: `build-qt/visual-service-baseline-0.6.93`. UI SHA `573C12BF3B9C2A2C8A3A7CEC0CB6DD5F3509D6F35DB48FCC20F0E11BF0015B6B`; Core SHA `A45CC5206549B689FBD5D20C311FBDC97C3701F38B77E4063F80DF0D5F9029F7`. The separate native console driver links those libraries; it is not current code with a theme toggle.

Native BEFORE at 100/200%, requested 640×520: exit 0, stderr empty. At 200% Rules preset command text is clipped, Vault grows to 640×660, CloudSettings grows to 640×768 and clips long checkbox captions. Banner's old 110-pixel maximum gives only a few text lines at 200%. Captures and logs: `build-qt/native-service-before-0.6.93` and matching `.out` / `.err`. Four 200% captures were visually inspected; all eight initial contexts were actually run.

## Text wrapping and intrinsic sizing

| Before | After |
| --- | --- |
| Long Rules preset controls share one compressed horizontal row | Shared measured FlowRow wraps individual commands; compatible pairs remain together, hidden commands consume no space |
| Forms grow with their complete content; errors may be inside the scrolled body | Four editor bodies scroll vertically; error and commit/cancel footer remains outside that scroll |
| Long labels and checkbox captions can be clipped | Wrapped form labels, short cloud captions with full warning/context, semantic Rules/Vault sections, adaptive path/day controls |
| Numeric editors can be compressed below their complete value/suffix width | Rules/Vault reserve polished intrinsic SpinBox widths; captions stack above fields when their measured width exceeds the preferred form space |
| Banner text height has a fixed maximum of 110 pixels | Font-aware minimum reserves four text lines, no fixed maximum; wrapping and inner text scrolling remain |
| History uses full ISO date in one wide column and unwrapped changes | Visible date/time occupy two lines; full ISO timestamp in tooltip/accessibility, wrapped changes and measured rows; empty/corrupt explanations with no orphan table |
| Stacked Cloud root row reserves unused vertical stretch | Field already expands horizontally; removal of explicit stretch keeps the stacked row compact (304 → 124 px, field/button gap 76 → 16 px at native 200%) |

## Keyboard and error feedback

| Before | After |
| --- | --- |
| Secondary/default behavior and Tab order are implicit | One actual primary/default Save, explicit Tab chains and local focused Return for secondary/Cancel; Banner Tab advances focus while Return inserts a line |
| Invalid preset name returns from the generic input | Compatible QInputDialog subclass keeps invalid draft open and focuses its field, with the original 1–48 printable-character rule |
| Required-field errors give a general message | Vault currency/day, Banner text and Cloud root feedback preserve draft and address the relevant field; write/security/transaction handlers remain |
| Two Qt folder-picker combos have no accessible name | The same Directory / ShowDirsOnly / DontUseNativeDialog picker has named location/type controls; actual accessible names and local Cancel are tested |

No new geometry/chart/table animation, theme or domain transaction was introduced. Palette and existing optional motion remain unchanged.

## Verification and limits

Final native Windows console driver: exit **0**, empty stderr. Seven scales **90/100/110/125/150/175/200%**, seven contexts per scale (Rules, populated/corrupt Rules-history source, Vault, Banner create/edit, Cloud): **49 main contexts × 5 inspections = 245 actual geometry/glyph/layout states**. Requested main size 640×520, same-instance wider→narrower reversal and draft/error reversal. Native screenshot output contains **52 PNGs** at 100/200%, not 52 separate full functional tests. `build-qt/native-service-accepted-0.6.94.out/.err`.

Measured checks cover complete numeric/date/current editor glyphs, wrapped text, body/footer/command bounds, no outer horizontal range, FlowRow pairs/hide/show/containment, sole default and actual painted primary, Tab/Backtab/local Return, multiline Return and required-field focus. Draft values survive resize; focused Cancel and ten Escape cancellations preserve all persisted file bytes, with successful snapshot reads explicitly checked. **Ten nested contexts** at 100/200%: two invalid preset names, six history states, two actual Qt folder pickers. The picker retains standard Qt text/dimensions and may exceed 640×520; only screen bounds, existing mode/options, command readability, accessible names and Cancel preserving parent root/draft are accepted. Its filesystem columns/theme are not redesigned. Forced 640×520 preset captures prove bounds/readability, not its natural preferred-size compactness.

Full Release Qt suite **1/1 PASS, 49.19 s** (49.34 total), **216 accessible dialogs**; `smoke_core: OK`. `build-qt/build-package-0.6.94-accepted.log`, preserved `build-qt/qt-suite-accepted-0.6.94.log`. Existing persistence, stale/locked-write, ID/security and rollback checks remain enabled. No assertions or timeout were weakened.

Intermediate failures are retained as diagnosis, not accepted evidence: preliminary native review found the Cloud path-row gap even though initial bounds assertions passed; a strict gap gate was added. Full and isolated offscreen runs then measured Cloud checkbox 604 px allocated vs 646 px intrinsic, Rules/Vault compressed numeric editors. Shorter caption/full context and intrinsic-width + measured stacked layout address actual backend metrics. Intermediate Ignored-label sizing attempts failed wrapping checks and were reverted. A later complete suite detected two unnamed folder-picker combos; the real picker is now named and tested instead of excluding it from the audit. Resolved fallback-font identity is not claimed from QFont::toString.

Fresh 31-file .94 package starts with only `C:\Windows\System32;C:\Windows` in PATH and Qt/QML plugin variables unset; normal/help/version exit 0, stderr empty, CLI/EXE metadata agree on **0.6.94**, qwindows.dll **6.8.3** present. Normal screenshot shows the synthetic profile, not Usage; help is explicitly requested. Startup probes use process Wait and are not native exposure/focus acceptance. [Release and real Setup verification](../../../releases/ForgeMirror-0.6.94.md), [persistent images/hashes](UI_SERVICE_EDITORS_EVIDENCE.json).

This does not accept wallet movements, shortcut forms, cloud preview/pull/update decisions, CloudConflict/StorageConflict, general confirmation/export/import dialogs, pipeline transition/bulk edit, all table/page states or timed motion. The native hover OPEN result and policy-blocked 18×7 page rerun remain unchanged.

## Usage screenshot: confirmed origin

The exact screenshot path `Z:\CPP\ForgeMirror\build-qt\ui-review-package-783471b11fe04f0692b043a833caf22d\ForgeMirrorQt.exe` exists: temporary QA EXE **0.6.88**, 3,739,136 bytes, SHA `7FF39F65712307155944F9E3211767A4C780744A070C83BEB89320F7F31156FA`. The original root session records an explicit `& $exe --help` at that path, without redirection, at **2026-09-29 22:37:09.571 UTC**. Evidence: original session `rollout-2026-08-28T02-03-20-01a04576-ade7-7012-a975-6d36038de444.jsonl:120602`.

The image is an intentional old-GUI help dialog, not proof of a crash or a normal startup defect. Checked Desktop/StartMenu Qt shortcuts have empty arguments and point to installed **0.6.87**. No live Qt processes existed at triage. Parser, shortcuts and production data were not changed. Previous ledger notes used a path missing `f` (`...11fe040692...`), incorrectly reporting the folder absent; those notes are superseded, not silently rewritten.
