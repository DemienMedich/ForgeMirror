# Служебные формы: конечный inventory

Срез исходников 30.09.2026, комплексный проход после проверенного checkpoint графиков и XP **0.6.93**. Это **семь уже существующих семейств**, а не новые модули или расширение [матрицы приёмки](UI_ACCEPTANCE_MATRIX.md). Checkpoint **0.6.94** принят только для четырёх редакторов и перечисленных вложенных окон: финальные native / suite / отдельная поставка прошли. Это не закрывает целые семейства; прочие варианты ниже остаются UNREVIEWED.

Scoped checkpoint **0.6.95 — NATIVE VERIFIED в указанном scope**: wallet adjustment/history, shortcut forms/pickers/безопасный quick menu, conflict resolvers и navigation hide/disable/user-off. [Decision audit](visual-audit/2026-09-30/UI_DECISION_FORMS_AUDIT.md) сохраняет genuine BEFORE, intermediate FAIL и prototype-only границу; final Qt/native/package проверки прошли, настоящий Setup0.6.95 создан, эквивалентный disposable-AppId lifecycle .94→.95→uninstall прошёл. Production .87 не обновлялась. Семейств по-прежнему семь; SPI/destruction/native hover и policy-blocked18×7 не закрываются.

| Семейство | Реальные варианты | Естественный существующий regression fit |
| --- | --- | --- |
| Правила | `QtRulesEditor.cpp`: исходные/изменённые значения; создать/загрузить/удалить preset; вложенный ввод имени; пустая/наполненная/повреждённая история; ошибка сохранения | `TestRulesEditor` |
| Хранилище и кошелёк | `QtVaultEditor.cpp`: настройки, пустая валюта, отсутствие дней, ошибка записи. `QtWindow.cpp::editWallet`: начисление/списание, превышение баланса, основание и подтверждение | `TestVaultEditor`, `TestPersonalWallet` |
| Баннер | `QtBannerEditor.cpp`: создание/редактирование, пустой/длинный multiline текст, stale/locked save и Cancel | `TestBannerEditor` |
| Ярлыки | Create-only форма в `QtWindow.cpp`, файл/папка и оба picker. Quick menu: запущен/остановлен/неизвестен/пусто. Памятка. Отдельного `QtShortcutEditor.cpp` нет | `TestShortcutFolderEditor`, `TestShortcutPersistence`, `TestQuickShortcutLauncher`, существующий driver памятки |
| Облачные решения | `QtCloudSettings.cpp`: enabled/autosync, длинный/пустой/пересекающийся root, picker, ошибка записи. UI push-preview / pull-confirm / update-confirm — в `QtWindow.cpp`; одноимённые CloudPull/PushPreview/Release cpp главным образом backend | `TestCloudSettings`, `TestCloudPushPreview`, `TestCloudPullTransaction`, `TestCloudReleaseUpdate`, `TestCloudQuickHeader`; backend PASS не заменяет проверку decision boxes |
| Конфликты | `QtCloudConflict.cpp`: семь существующих tabs; local/cloud/missing; empty/populated snapshots; apply/push/restore confirmations; переход tasks/pipeline. `QtStorageConflict.cpp`: cloud/local и Cancel-default confirmation | `TestCloudConflictResolver`, `TestStorageConflictResolver`; сохранить fixture/ID/rollback assertions |
| История, экспорт, диагностика и общие решения | `QtWindow.cpp`: история событий / XP, фильтры/no-match, wallet history, cleanup inventory, существующие CSV/report exports, удаления/bulk-delete/пересчёт/clear-log и сообщения ошибок. Startup import Yes/No/Cancel — `qt/main.cpp`; `QtWorkspaceImport.cpp` — backend | `TestPersonalWallet`, `TestQtStorageHealthReport`, `TestQtVisibleTaskExports`, существующие export/deletion/recovery drivers, `TestWorkspaceImportSnapshot` |

## Scoped checkpoint 0.6.95 — NATIVE VERIFIED в указанном объёме

| Существующий scope | Подтверждённое изменение / результат / остаток |
| --- | --- |
| Wallet adjustment / history | Scroll body + permanent footer, полный wrapped profile/currency/preview, intrinsic numeric и maximum1e9, required memo/overbalance, credit/debit confirmation, readonly empty/populated history. Final seven-scale native, keyboard, Cancel/successful byte reads и фактическая persistence прошли; general event/XP history сюда не входит |
| Shortcut create / help / Qt pickers / quick menu | Draft/path validation, локальные Save/Cancel/Tab, реальные прежние QFileDialog modes/options, все17 help actions, intrinsic table HFW, full statuses/ID/context и phase-gated menu sizing/paint. Final suite/native прошли; четыре actual popup100/200, post-paint rows582/595px,0 запусков, unchanged bytes. Offscreen prototype579/599 отделён от native evidence; полные page modes и внешний shortcut launch не приняты |
| Cloud / storage conflicts | Все семь original tabs, readonly wrapped comparison/backups, full consequences, empty/missing/malformed states, measured styled Restore cell geometry, безопасный Close/Cancel default и фактически исполненные nested apply/cancel decisions. Final seven-scale/nested native прошёл. Непроигранные push/restore/catalog-pair variants, Cloud push-preview/pull/update decisions остаются отдельно |

Final decision run независимо повторил **404 main checks /83 modal contexts /20 confirmations**, exit0/stderr0,70 PNG при семи90–200%; прежние failed runs с такими же counters не принимаются задним числом. Это не404 отдельные формы и не просмотр всех70 изображений: проверены11 представительных final native и2 package кадра. Full Qt1/1 PASS56,02 с /323 доступных диалога, core OK. Evidence index содержит153 artifacts /120 PNG, из них8 prototype-only или historical failed prototype,0 hash mismatch. Исторический .94 PASS ниже не переписывается в .95 PASS.

Конечный остаток после этого source pass не расширяется: Rules прочие preset/save errors; Vault write failure; Banner stale/locked; cloud preview/pull/update и непроигранные conflict modes; general history/export/diagnostics/import/delete decisions. Pipeline transition/bulk task forms и page/icon/hover/motion policy gates остаются в общей матрице.

## Scoped checkpoint 0.6.94 — PASS в указанном объёме

| Затронутый scope | Финальное native evidence / остаток |
| --- | --- |
| Правила | PASS: основная форма с пустой, наполненной и повреждённой history fixture — семь масштабов, узкая / рабочая ширина / обратный переход, glyph/bounds/default/footer и сохранённый draft. Вложенные preset name с ошибкой и история трёх состояний — 100/200%, локальные Return / Cancel / Close. Полный функциональный suite прошёл; остальные preset/save/error состояния не получают визуальный PASS автоматически |
| Настройки хранилища | PASS: семь масштабов, читаемые checkbox / числовые controls, пустая валюта и отсутствие дней, адресная ошибка и draft / Cancel byte equality. Начисление / списание / превышение баланса и история кошелька из `QtWindow.cpp` не входят |
| Баннер | PASS: create/edit с multiline текстом — семь масштабов, постоянный footer, required-field focus, Return добавляет строку, draft переживает изменение ширины, Cancel / Escape сохраняют байты. Stale/locked tests в полном suite сохранены; это не визуальная приёмка всех error states |
| Облачные настройки | PASS: семь масштабов, длинный / пустой / пересекающийся root, native короткие checkbox captions и полный видимый warning, footer/default/Tab/draft/Cancel, измеряемый компактный gap stacked path-row. Реальный Qt picker с `DontUseNativeDialog` — 100/200%, screen bounds, readable commands, локальный Cancel Return, root / parent draft не меняются. Текущая шрифтовая раскладка повторно проверена после промежуточных glyph-отказов. Preview / pull / update и conflict resolver не входят |

Доказательства и пределы: [UI_SERVICE_EDITORS_AUDIT.md](visual-audit/2026-09-30/UI_SERVICE_EDITORS_AUDIT.md). Исходные UI/Core libraries и все затронутые исходники **0.6.93** сохранены отдельно до правок; финальный native run `build-qt/native-service-accepted-0.6.94` после текущих исправлений завершился exit 0 с пустым stderr. На семи масштабах проверены **245 main-layout состояний**, **10 вложенных контекстов** и **10 Escape cancellations**; сохранены **52 PNG**, не 52 отдельные полные функциональные проверки. Full Qt **1/1 PASS, 49,19 с** (49,34 с суммарно) / **216 доступных диалогов**, core OK. Это scoped proof, не новая общая 18×7 page-приёмка. Приёмка оставшихся значков / hover, timed motion / системной политики и остальных страниц остаётся в общей матрице.

Поставка **0.6.94** подтверждена отдельно: свежий 31-файловый пакет, clean-PATH normal / help / version exit 0 / пустой stderr, qwindows.dll; настоящий `dist/ForgeMirrorSetup_0.6.94.exe` собран. Эквивалентный disposable-AppId install .93 → update .94 → uninstall прошёл, test-каталог / HKCU удалены, внешний marker не изменился. Это не byte-identical release Setup и не обновление пользовательской .87. [Точный Setup, SHA и lifecycle](../releases/ForgeMirror-0.6.94.md).

## Общий UI-гейт

Применимые формы проверяются при 640×520 и рабочей ширине, на 90 / 100 / 110 / 125 / 150 / 175 / 200%, с длинными русскими строками. Нужны читаемый текст и реальные внутренние bounds, footer/default/local Cancel Return, адресная ошибка с сохранённым draft и отмена без записи. Прямой `click()` в старом smoke не доказывает этот результат. Domain tests, ID, security/rollback assertions не ослабляются.

Для штатного native `QFileDialog` допускается записанное исключение: системный picker сохраняется, не перерисовывается в Qt-theme. Диалоги с `DontUseNativeDialog` к такому исключению не относятся. Informational/confirmation формы не получают фиктивных обязательных полей или действий для унификации.

## Отдельный уже запланированный остаток рабочих форм

`QtPipelineTransition.cpp` / `TestPipelineTransition` и массовые изменения задач в `QtWindow.cpp` / `TestBulkTaskEditsUi` **не закрываются** проходом завершения / ручного XP. Они остаются в существующей строке рабочих форм матрицы. Остальные page states, icons/hover и timed motion также не являются частью этого inventory и остаются открытыми на своих строках.
