# Служебные формы: конечный inventory

Срез исходников 30.09.2026, следующий комплексный проход после графиков и XP. Это **семь уже существующих семейств**, а не новые модули или расширение [матрицы приёмки](UI_ACCEPTANCE_MATRIX.md). Статус всех строк здесь — UNREVIEWED по полной визуальной приёмке, пока нет отдельного результата.

| Семейство | Реальные варианты | Естественный существующий regression fit |
| --- | --- | --- |
| Правила | `QtRulesEditor.cpp`: исходные/изменённые значения; создать/загрузить/удалить preset; вложенный ввод имени; пустая/наполненная/повреждённая история; ошибка сохранения | `TestRulesEditor` |
| Хранилище и кошелёк | `QtVaultEditor.cpp`: настройки, пустая валюта, отсутствие дней, ошибка записи. `QtWindow.cpp::editWallet`: начисление/списание, превышение баланса, основание и подтверждение | `TestVaultEditor`, `TestPersonalWallet` |
| Баннер | `QtBannerEditor.cpp`: создание/редактирование, пустой/длинный multiline текст, stale/locked save и Cancel | `TestBannerEditor` |
| Ярлыки | Create-only форма в `QtWindow.cpp`, файл/папка и оба picker. Quick menu: запущен/остановлен/неизвестен/пусто. Памятка. Отдельного `QtShortcutEditor.cpp` нет | `TestShortcutFolderEditor`, `TestShortcutPersistence`, `TestQuickShortcutLauncher`, существующий driver памятки |
| Облачные решения | `QtCloudSettings.cpp`: enabled/autosync, длинный/пустой/пересекающийся root, picker, ошибка записи. UI push-preview / pull-confirm / update-confirm — в `QtWindow.cpp`; одноимённые CloudPull/PushPreview/Release cpp главным образом backend | `TestCloudSettings`, `TestCloudPushPreview`, `TestCloudPullTransaction`, `TestCloudReleaseUpdate`, `TestCloudQuickHeader`; backend PASS не заменяет проверку decision boxes |
| Конфликты | `QtCloudConflict.cpp`: семь существующих tabs; local/cloud/missing; empty/populated snapshots; apply/push/restore confirmations; переход tasks/pipeline. `QtStorageConflict.cpp`: cloud/local и Cancel-default confirmation | `TestCloudConflictResolver`, `TestStorageConflictResolver`; сохранить fixture/ID/rollback assertions |
| История, экспорт, диагностика и общие решения | `QtWindow.cpp`: история событий / XP, фильтры/no-match, wallet history, cleanup inventory, существующие CSV/report exports, удаления/bulk-delete/пересчёт/clear-log и сообщения ошибок. Startup import Yes/No/Cancel — `qt/main.cpp`; `QtWorkspaceImport.cpp` — backend | `TestPersonalWallet`, `TestQtStorageHealthReport`, `TestQtVisibleTaskExports`, существующие export/deletion/recovery drivers, `TestWorkspaceImportSnapshot` |

## Общий UI-гейт

Применимые формы проверяются при 640×520 и рабочей ширине, на 90 / 100 / 110 / 125 / 150 / 175 / 200%, с длинными русскими строками. Нужны читаемый текст и реальные внутренние bounds, footer/default/local Cancel Return, адресная ошибка с сохранённым draft и отмена без записи. Прямой `click()` в старом smoke не доказывает этот результат. Domain tests, ID, security/rollback assertions не ослабляются.

Для штатного native `QFileDialog` допускается записанное исключение: системный picker сохраняется, не перерисовывается в Qt-theme. Диалоги с `DontUseNativeDialog` к такому исключению не относятся. Informational/confirmation формы не получают фиктивных обязательных полей или действий для унификации.

## Отдельный уже запланированный остаток рабочих форм

`QtPipelineTransition.cpp` / `TestPipelineTransition` и массовые изменения задач в `QtWindow.cpp` / `TestBulkTaskEditsUi` **не закрываются** проходом завершения / ручного XP. Они остаются в существующей строке рабочих форм матрицы. Остальные page states, icons/hover и timed motion также не являются частью этого inventory и остаются открытыми на своих строках.
