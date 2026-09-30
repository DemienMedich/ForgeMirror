# Qt decision forms и жизненный цикл навигационного маркера — 0.6.95

**0.6.95 — NATIVE VERIFIED в указанном scope.** Срез 30.09.2026: пятая общая сборка, отдельные native decision/motion/menu прогоны и package verification завершились успешно по конечным libraries. Настоящий Setup создан; эквивалентный disposable-AppId lifecycle .94 → .95 → uninstall прошёл отдельно. Ни этот checkpoint, ни число проверок не означают завершение всего UI-плана или просмотр каждого снимка. Полная переработка UI остаётся активной.

Четвёртый suite прошёл, но отдельный prototype после этого выявил paint-cache poisoning; поэтому его PASS не является результатом конечного phase-gated source freeze. Текущие QuickShortcut regressions выполняют два render на каждом переходе100→200→100→200, проверяя natural hint и actual row containment; final native driver повторяет checks после grab с общим widest-status budget. Эти predicates не заменяются capped hint и не ослабляются.

## Финальные результаты по фактическим артефактам

| Гейт | Результат |
| --- | --- |
| Полный Qt / core suite конечного source freeze | **PASS**: build exit0, Qt1/1, **56,02 с** (56,18 с суммарно), **323 доступных диалога**, core OK. `build-qt/qt-suite-accepted-0.6.95.log` и `build-package-0.6.95-fifth.log`; smoke EXE SHA записан в evidence index |
| Native decision matrix конечных libraries | **exit0 / stderr0**: **404 main-state checks /83 modal contexts /20 confirmations**, семь90/100/110/125/150/175/200%, **70 PNG**. `build-qt/native-decision-accepted-0.6.95.out/.err`; это не404 уникальные формы |
| Native motion / quick menu | Каждый **exit0 / stderr0**, по **4 PNG**. Motion: allowed1, реальный midpoint на33ms y21 при target40, reversal32→32, hide/disable Stopped, user-off immediate. Menu: четыре actual native popup100/200 × empty/populated, post-paint rows582/595px, empty242/446px, full status/context/glyphs, **0 запусков**, успешное чтение и неизменные bytes |
| Отдельный package normal / help / version без Qt в PATH | **PASS**: 31 файл, `PATH=C:\Windows\System32;C:\Windows`, все три exit0, stderr0; version output и EXE metadata **0.6.95**. `build-qt/package-0.6.95-runtime.json`, `package-0.6.95-proof.json`; два package-кадра просмотрены. Startup probes не заменяют интерактивную native-приёмку |
| Настоящий Setup | **Собран**: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.95.exe`, версия **0.6.95**, **12675116 bytes**, SHA `AFD449B77691848AE8194BA35C4774F9DECB4DC77426A1B393EF067FAFBDAAD6`; `build-qt/setup-0.6.95-proof.json` |
| Изолированный install → update → uninstall | **PASS**, прямой structured verifier output, `DerivedFromLog=false`: `build-qt/lifecycle-0.6.95-result.json`, token `5271272C8C484EA5BB1995AD42D64D95`. .94/.95 installed metadata, HKCU и actual window titles совпали; .95 CLI также0.6.95. Оба clean-PATH startup exit0/stderr0, qwindows совпал. Uninstaller/helper exit0, test install/HKCU удалены, external marker SHA неизменен. Это **эквивалентные test Setup с disposable AppId**, не byte-identical release Setup и не update пользовательской .87 |

Поставляемый EXE: `Z:\CPP\ForgeMirror\package-qt-0.6.95-release\ForgeMirrorQt.exe`, **4098560 bytes**, SHA `F289043BA32262C02CAEC2B24DD972DC8F44C3CB3164827D08C757D46081DD43`. `platforms/qwindows.dll` — **6.8.3.0**, SHA `12577A7C4F2230BBEA53F279573D7E3D1A08D631197855481E1E1580F61BB877`. Test Setup .94:12653929 bytes / SHA `20496B00DE5D82B30F589443F10DAD3F8ECEF1EBECD7063BBC1245C861AFCA36`; .95:12675191 bytes / SHA `F6A6B725B998D8F5C7EDBDFDA7EF6C5C3001C341DE917EFFB974E46A5A234872`. Разницу test/release identities сохраняем явно. Structured record .94 не содержит CLI text; его версию подтверждают metadata/HKCU/title, а не выдуманный CLI output.

Инсталляция пользователя0.6.87, её ярлыки, production workspace, scheduler и Hub в этом проходе не менялись. ImGui-baseline сохраняется. Реально просмотрены **11 представительных final native кадров**: root — wallet maximum200, history200, help200, menu200, cloud100, wallet credit confirmation100, shortcut100; дополнительный reviewer — cloud200, storage200, shortcut error200 и wallet debit confirmation200. Также root просмотрел **два package-кадра**. Это не утверждение о визуальном просмотре всех70 native PNG.

## Объём и происхождение BEFORE

Изменены `qt/QtCloudConflict.cpp`, `qt/QtStorageConflict.cpp`, соответствующие блоки `qt/QtWindow.cpp` и объявления lifecycle overrides в `qt/QtWindow.h`. Регрессии расширены в `tests/smoke_qt.cpp`; standalone motion и quick-menu probes находятся в ignored `build-qt`. Палитра, доменные права, ID, transaction/recovery и callback сохранения не заменяются стилизацией.

Настоящий BEFORE: сохранённые UI/Core libraries и Qt-исходники **0.6.94** в `build-qt/visual-decision-baseline-0.6.94`, отдельный console driver с явным линкованием этих libraries. Native BEFORE завершился exit 0 с пустым stderr и **24 PNG** при 100/200%, requested 640×520, synthetic `QTemporaryDir`; UI-изменения отменялись. Это не нарисованный макет и не повторная сборка нового UI под старой подписью.

| Артефакт baseline | Bytes | SHA256 |
| --- | ---: | --- |
| `ForgeMirrorQtUi.lib` | 44532618 | `67CFCEC5BF1EBD11D2A5BC320809EEF8F48E54C872ACE8BD30D298805E0DDB68` |
| `ForgeMirrorQtCore.lib` | 26376318 | `084EF6CFD8130490E29737124CD716DEC910C5B2BD3A2C318E911864C94C26E3` |
| `driver-build/Release/decision_baseline.exe` | 3965952 | `D15CD5362068AC6C8EFAC07EFC6BB01798180A5DD7A0808C1512596FE9591AC3` |

Исходные identities, source/EXE hashes и capture hashes: `build-qt/visual-decision-baseline-0.6.94/native-before-proof.json`. Реально просмотрены четыре BEFORE200 вида: cloud tasks, storage, wallet credit и shortcut help. Наличие 24 файлов не является утверждением о просмотре всех 24.

## BEFORE → реализованное решение, по принципам

### Полный контекст и предсказуемое перенесение строк

| Реальный недостаток BEFORE | Изменение и проверенный scoped результат |
| --- | --- |
| Cloud requested640×520 становился720×520, Storage —760×520. Source/target path и summary сокращались; резервные снимки при200% показывали практически один header | Оба resolver используют `QtScrollableDialog`: scroll body и постоянный footer, preferred640×520, screen bounds. Семь исходных cloud tabs и колонки/ID сохраняются. Сравнение и backup cells рисуются/измеряются общим `QTextLayout` с Anywhere wrap; headers, реальные styled commands и доступное место задают widths. Допускается внутренний table H-scroll, когда реальные minima не помещаются; outer H-scroll не принимается |
| Длинные команды занимали недостаточную ширину, restore-cell118×42 при hint204×42 | Flow rows переносят отдельные вторичные команды. Page/Restore/Close buttons полируются после окончательного parenting и получают измеряемую минимальную высоту. Restore allocation учитывает реальные `SE_ItemViewItemText` style/QSS insets и intrinsic command size; геометрия index widget центрируется по высоте, текст не растягивает command на высоту многострочной summary |
| Длинный текст решения мог вытеснять команды QMessageBox | Cloud/storage confirmation остаются QMessageBox-compatible с прежними результатами Yes/Cancel и безопасным Cancel default. Полный `.text()` сохранён; видимый readonly `conflictConfirmBody` переносит source/target, поддерживает Tab и vertical scroll. Screen bounds, минимум трёх текстовых строк с учётом viewport/document/style chrome, постоянные команды — измеряемый контракт |
| Длинные профиль/валюта в wallet обрезались; суффикс валюты отнимал место у суммы | Wrapped `walletProfile` и отдельный `walletCurrency`, полный tooltip/accessibility. Профиль, currency, hint и preview резервируют фактическую HFW высоту; form сообщает intrinsic minimum. Валюта вынесена из spin suffix, но числовые decimals/range/group separator/value и учёт денег сохраняются |
| Wallet history и памятка не обеспечивали полный вид строк | Wallet history сохраняет четыре колонки, полные amount/reason, full-context tooltip/accessibility, date в трёх строках с полным ISO context. Explicit empty history отличается от populated. Памятка сохраняет все17 сочетаний и действий, readonly/NoSelection/ElideNone; full-table HFW рассчитывается до body layout, row height берётся из styled delegate при фактическом column width, footer Close остаётся постоянным |
| Shortcut create имел обычный фиксированный form flow | Scroll body и постоянный Save/Cancel footer, wrapped hint/field labels, adaptive path commands, локальная адресная ошибка и сохранение draft. Исходные label/path/picker IDs и правила проверки пути сохранены |

### Числа и понятная иерархия действий

| BEFORE / подтверждённый риск | AFTER и фактическая проверка |
| --- | --- |
| Currency suffix и длинный контекст отнимали место у wallet amount; крупное число нельзя считать читаемым по одному sizeHint | Currency вынесена в wrapped label. Сохранены диапазон **0,01…1 000 000 000**, decimals2, группировка и value; native intrinsic width/height/editor glyph assertions прошли, включая maximum1e9 и полные history amounts. Preview показывает баланс до/после; overbalance даёт явную причину и disabled Apply, исходный повторный domain guard остаётся |
| Обычная форма и terse confirmation не давали компактной иерархии с полной суммой | Apply — единственная primary/default; required reason error сохраняет draft и переводит focus адресно. Wallet confirmation теперь явно содержит операцию, **числовую сумму**, профиль, баланс и основание, No безопасный default. Native credit/debit accept/cancel и persistence прошли; чужие domain transactions не менялись |
| Длинные conflict commands не помещались, опасные команды не должны становиться implicit Return target | ApplyCloud / AcceptCloud остаются primary; Close — единственный безопасный default; Apply/Push/Open/Restore noAutoDefault. Secondary caption «Отправить локальную» компактна, полные последствия доступны в tooltip/accessibility/confirmation. Native bounds/default/focused-command проверки прошли в указанном scope |
| Menu first-run action rows обрезались; sizing-only prototype не исправлял поздний paint-cache pass | Общий full-status budget + actual styled QMenu overhead + bounded label elision; все исходные ID/run-state/icon/callback сохранены. Полные имя/путь/статус доступны в tooltip/statusTip, `&` literal escaped; empty guidance disabled. Phase-gated sizing/paint и два post-paint render проверены отдельно: final native rows582/595px, без status clipping или запуска action |

Palette/card система и бизнес-функционал не расширялись. Данные не сокращаются ради визуальной компактности.

#### Quick menu: sizing-only prototype FAIL → phase-gated prototype PASS

В отдельном **offscreen prototype**, с копиями действующих ShortcutMenu/callback/icon/QtTheme и synthetic providers, воспроизведено расхождение: override только `sizeHint()` давал natural579px при100% и599px при200%, но последующий paint/geometry проход расширял action rect до722/896px. Это **prototype-only FAIL**, не native product acceptance; пустой prototype stderr не отменяет записанный FAIL в stdout.

Диагноз основан на Qt6.8.3 primary code: QMenu добавляет общую tab-width после item sizing и использует её в style option; Fusion учитывает reserved width в sizing и отдельно в painting; QSS может передавать расчёт базовому style. Конкретный product вывод подтверждён отдельным prototype, а не одним чтением Qt. См. [QMenu sizing/paint](https://github.com/qt/qtbase/blob/v6.8.3/src/widgets/widgets/qmenu.cpp), [Fusion menu sizing/paint](https://github.com/qt/qtbase/blob/v6.8.3/src/widgets/styles/qfusionstyle.cpp), [QSS style fallback](https://github.com/qt/qtbase/blob/v6.8.3/src/widgets/styles/qstylesheetstyle.cpp).

Текущий `ShortcutMenu` различает sizing и painting: reserved width не учитывается повторно во **всех geometry passes**, включая вызовы вне `sizeHint()`; перед paint обновляются dirty action rects под sizing policy, а непосредственно paint получает **полную** tab/status column. Native renderer/индикаторы/данные не заменяются ручной отрисовкой меню.

Prototype100→200→100→200 завершился exit0: natural/rendered widths **579/599/579/599**,12 action checks, полные «Запущена», «Не запущена», «Статус недоступен», без triggered action; stderr0. `build-qt/menu-sizing-prototype/runtime-proof.json` прямо помечает **Offscreen prototype only; not product or native acceptance**. Его stdout SHA `7417DBFBAE13B2D36FF4B62DC93A5E3F2047DBB9B4FBE4FF72DAAA3A3F79E459` и EXE SHA `131DEB1644BBE3B77CB589B1C0827BBEB83A610A1630A38AFAE0F59C9B53BC44` — identities prototype, не поставляемого EXE.

**Не использованы** capped/fake `sizeHint()`, сокращение статусов, исключение строк за popup bounds из теста или ослабление glyph/status assertions. Максимальная ширина остаётся safety bound, но natural и реальные action rects должны помещаться сами. Отдельный **final product native run прошёл** по конечной UI library; его582/595px и четыре popup contexts не смешиваются с prototype579/599px.

### Клавиатура, локальная отмена и безопасные picker

| BEFORE / граница безопасности | AFTER и проверка |
| --- | --- |
| Обычный dialog layout не обеспечивал постоянный footer; secondary Return нельзя незаметно направить на чужой Save/Apply | Существующий `QtScrollableDialog` local Return/Enter route: focused enabled visible command активируется локально. Native Cancel/Close Return не исполнил Apply другого контекста; confirmation Yes/Cancel или Yes/No contract и безопасный default сохранены |
| Draft и focus могли потеряться при смене ширины; одно byte equality без successful read недостаточно | Native Tab/Backtab reason→Apply, path→file-picker, required/malformed focus и draft640×520→1000×640→640×520 прошли. Cancel проверен без записи: отдельно successful snapshot open/read, затем exact bytes. Native available-screen bounds сохранены; offscreen wide может быть bounded780, а не1000 |
| Browse commands были частью обычного form flow; picker нельзя подменять имитацией | Adaptive path commands и wrapped fields с теми же IDs; **реальные Qt QFileDialog** `shortcutFilePicker`/`shortcutFolderPicker`, исходные DontUseNativeDialog/FileMode/ShowDirsOnly. Native100/200 actual controls, glyph/command/screen bounds и local Cancel Return сохранили родительский path. Это не native Windows picker exemption и не обещание640px для picker |
| Popup проверка не должна ради QA запускать внешние программы или менять fixtures | Actual InstantPopup/active QMenu, empty/populated100/200, full statuses/ID/icon/context, screen/row bounds и Escape прошли. **0 shortcut launches**, successful reads и bytes unchanged во всех четырёх contexts. Реальный запуск сторонней программы исключён из этого безопасного UI proof |

### Прерываемая motion-анимация

Навигационный marker остаётся одним presentation-only элементом:180ms OutCubic, без focus/mouse interception и без анимации размеров всей страницы/таблицы. Ранее reversal начинался с реальной текущей геометрии и не требовал product fix.

| Archived native BEFORE | Current native AFTER |
| --- | --- |
| Reversal stop32→start32 проходил, но после `hide()` на37ms и disable animation оставалась Running | Узкий lifecycle fix останавливает marker и переводит к selected-row target при hide/disable/невозможном target; per-frame guard повторно проверяет `IsQtMotionAllowed`. Final actual event loop: allowed1,33ms midpoint y21→target40, reversal32→32, hidden state0/y40, disabled state0/y6. User-off state0 сразу, target40; четыре PNG, exit0/stderr0 |

Исходная навигация/focus/выбор не менялись, reversal не исправлялся. Принудительное `setCurrentTime` не заменяет native время; disclosure evidence не заимствуется. **Destruction lifecycle не закрыт**, Windows SPI setting не переключался: эти общие policy-гейты остаются открытыми.

## Промежуточные FAIL сохраняются, а не переименовываются в PASS

| Наблюдавшийся отказ | Фактическое доказательство / причина и текущее исправление |
| --- | --- |
| Первый full suite: Cloud Apply height28 вместо требуемых40 | Поздний QSS reset intrinsic min-height после reparenting. Source fix: после final parent ensurePolished + max(scaled40, styled sizeHint); затем table refit. Пятый final suite и native matrix прошли |
| Restore cell90 actual153×36 при hint160×25;200 actual342×208 при hint358×54 | `decision-diagnostics-second-0.6.95.err`: style item padding съедал ширину index widget, хотя column рассчитывался с одним scaled8 gap. Текущий delegate измеряет actual editor contents insets; intrinsic assertions не снижены |
| Storage keepLocal200 width763 при body604 narrow /744 wide | Исходная длинная caption сама превышала доступный body. Сокращена caption, полная семантика сохранена; проверка bounds остаётся |
| ShortcutHelp reversed200: table origin0,260, size604×1521, body604×1538 | Позднее изменение table minimumHeight создавало неполную body allocation. Текущий full-table HFW сообщает требуемую высоту до layout; нельзя исправлять такой отказ ослаблением body-bounds assertion |
| Wallet150: hint66px при required89; profile66 при135; currency43 при66 | Wrapped label allocation не обеспечивала реальный HFW. Source резервирует фактическую высоту, form minimum constraint; итоговая обратимость narrow/wide проверяется повторно |
| Cloud confirmation200 viewport622×42 при line31 | Три строки требуют минимум93px viewport. Source задаёт измеряемый body minimum с document/style chrome; окончательная геометрия и glyph assertion остаются обязательными |
| Quick menu first run: action rows выходили за popup containment при100/200 | Сохранены `quick-menu-probe-0.6.95/first-run/native-images` и nonempty stderr. Общая status column и real style overhead измеряются заново; final menu stderr должен быть пустым |
| Archived navigation hide/disable продолжали анимацию | `motion-decision-probe-0.6.94/native-corrected.out/.err` — historical known FAIL. Отдельный final current-library timed proof подтвердил Stopped и endpoints; исторический FAIL сохраняется |
| Первый lifecycle QA давал пустой .NET MainWindowTitle при Hidden start | Отдельный process-scoped Win32 probe подтвердил настоящее .94 title. Исправлен **verifier**, не product: EnumWindows только для launched PID, включая hidden окна, и отдельная проверка stderr. Accepted rerun прямой structured output exit0; SHA verifier `774A54747B4F638F28250B956599BB0F143AB847A9B8A2E8C9D59A3DDF23DDA8` |

`build-qt/native-decision-first-0.6.95`, `build-package-0.6.95-second.log`, `decision-diagnostics-second-0.6.95.err` — не финальная приёмка. Диагностические дополнения печатают actual/hint/min и реальные coordinates; существующие glyph/width/bounds assertions не ослаблены. Styled table glyph measurement получает `option.widget=table`, чтобы использовать actual widget context; final suite и native повторно прошли по конечному коду.

## Проверенный regression scope и точные пределы счётчиков

`TestQtDecisionFormsLayout` / `FORGEMIRROR_UI_AUDIT_DECISION_FORMS`: семь масштабов90/100/110/125/150/175/200, main forms640×520→working width→640×520, full glyph/command/body/footer bounds, одна primary/default, readonly tables/ID, intrinsic numeric и index-widget allocation, required/malformed/missing/empty/populated/maximum states. Snapshots требуют успешного open/read и точного byte equality. Nested100/200 включает реальные Qt picker, malformed confirmation→warning и apply/cancel callback, wallet credit/debit confirmation и persistence.

Прежние failed runs уже выдавали404/83/20, но не принимались по этим счётчикам. **Final accepted run** независимо повторил **404 main-state checks /83 modal contexts /20 confirmations**, exit0/stderr0; это счётчики checks/contexts, **не404 уникальных окон**. Сохранено70 final decision PNG; список11 реально просмотренных представительных native кадров указан выше отдельно.

Reviewed base persistence script `build-qt/persist-decision-evidence-0.6.95.ps1` и отдельный prototype supplement завершились exit0 после final native PASS. [Evidence index](UI_DECISION_FORMS_EVIDENCE.json) содержит **153 artifacts /120 PNG /12 native-run logs /4 prototype logs**:24 before decision,70 after decision,4 before motion,4 after motion,4 after menu,4 historical menu,2 package,4 prototype-only PASS и4 failed historical prototype PNG. Все153 сохранённых SHA повторно сверены:0 mismatch. Последние8 prototype PNG **не являются native acceptance**. Original source/library/driver identities, linked baseline driver/CMake hashes, bytes/SHA/pixel dimensions и distinct groups сохранены; historical FAIL не отбрасывается. Index сам не оценивает full suite или Setup/lifecycle: их отдельные actual artifacts перечислены выше.

Final linked UI library SHA `12E44B48252E51BF55E6254C128011C1124E5A3286D18619971FA93D08D62713`, Core `7E77CFAE83EA041F6BD8E7DB624BACFB7430BABDF629D3CAE272BCB1352A05DB`, smoke EXE `6ABDD58657849C7D6002947D76904A00DE526C6B3FC5B6CB077255DCD1FC7BA7`; native motion driver `E2C6196FB1A5BB708FAE7B8C4A5912DBFFBBE6BE57F2480A680FB80CF94CAFCD`, quick-menu driver `6404AD0E0EC8D7389CAC2D7882B71560C7A5D64EC242221B11274AAAE4F4CF81`. Current source hashes находятся в index, identities не подменяются prototype EXE.

## Что этот checkpoint закрывает, а что остаётся

Scoped строки: wallet adjustment/history; shortcut create/help/Qt pickers и безопасный quick-menu popup; cloud/storage conflict main renderers и **фактически исполненные** nested decisions; навигационный marker hide/disable/user-off lifecycle. Не называть это завершением всех семи служебных семейств.

Остаток существующего inventory без новых модулей:

- Rules прочие preset/save-failure варианты; Vault write-failure; Banner stale/locked visual states. Старые functional tests не дают им автоматический visual PASS.
- Cloud push-preview / pull / release-update decisions; непроигранные push/restore/catalog-pair conflict confirmation modes. Общий confirmation renderer можно принять в указанном объёме, но не заявлять исполнение всех опасных callbacks.
- General event/XP history и фильтры/no-match, exports, cleanup/diagnostics, startup import и общие delete/bulk-delete/recount/clear-log/error decisions. Wallet history закрывает только свою форму.
- Pipeline transition и bulk task edits остаются в уже существующей очереди рабочих форм.
- Page/table/icon состояния, native hover **OPEN**, policy-blocked18×7 и literal200 rerun остаются в общей матрице. **Windows SPI setting не переключался**, destruction lifecycle не принят; системная policy acceptance не заявляется. Quick-menu prototype и source lifecycle fix не снимают эти ограничения.

Этот audit фиксирует **проверенный scoped checkpoint0.6.95** и перечисленные исключения. Он не объявляет завершение всей переработки UI, просмотр всех70 decision PNG, byte-identical release Setup lifecycle или обновление production. Prototype PASS и historical failed counters отделены от final native results. Source freeze и невмешательство в production сохраняются.
