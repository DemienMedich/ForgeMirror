# Единый стиль выбора файла и папки ярлыка

05.10.2026, исходный кандидат поверх0.6.104; установленная версия не изменена.

| До | После |
| --- | --- |
| Стандартные разнородные toolbar icons Qt в выборе ярлыка | Тот же линейный набор6команд, что в экспорте, через общий PrepareQtFilePicker |
| Путь при200% зажат до Z:…or | Адаптивная строка показывает путь отдельно от toolbar |
| Open/Choose/Cancel, File name/Directory | Открыть/Выбрать/Отмена, Имя файла/Папка; экспорт сохраняет Сохранить |

[До200%](visual-audit/2026-10-05/shortcut-picker-before-200.png), [после200%](visual-audit/2026-10-05/shortcut-picker-after-200.png). Также просмотрен shortcut-picker-style-after/shortcut-picker-file-100.png. Системные имена колонок/типов Qt остаются английскими: полной локализации нет. Файловые значки папок/дисков оставлены как содержимое, не заменены иконками команд.

PrepareQtExportPicker сохранён как совместимый wrapper. Существующие режимы ExistingFile/Directory/ShowDirsOnly, модели, фильтры и возврат выбранного пути не менялись. Нативные системные диалоги helper не трогает. Для выбранных Qt диалогов переиспользуются accessible names/tooltips и disabled/selected варианты общего набора.

Сборка3399 exit0. Native DECISION_FORMS98293 exit0:404states/83modal contexts/20confirmations. После добавления проверок AcceptOpen, подписей Открыть/Выбрать и наличия имён/tooltip/icon шести команд — build60181 exit0, native7268 exit0 с теми же counters. Прежние проверки Cancel без изменения draft/байтов сохранены. CoreOK, shortcut-picker-core.log. Полный Qt50106 пока выполняется; отдельная поставка этого изменения ещё не создана.

Полный Qt50106 exit0:9/9 PASS191.77с, включая export pickers. Лог shortcut-picker-full.log. Source checkpoint проверен; VERSION и установленный EXE не менялись.

## Выбор папки синхронизации

05.10.2026: QtCloudSettings также использует PrepareQtFilePicker. Режим Directory, ShowDirsOnly, начальный путь и специализированные accessible descriptions сохранены. Все восемь явно создаваемых non-native pickers теперь используют общий стиль.

До: стандартная панель команд Qt в отдельном оформлении. После: общий набор линейных иконок, адаптивная строка пути, акцентная кнопка «Выбрать» и вторичная «Отмена». Просмотрен build-qt/cloud-picker-style-after/cloud-200-picker.png; системные названия колонок Qt остаются английскими. Это унификация оформления, не полная локализация.

Сборка64088 exit0. Native SERVICE_EDITORS оставил 52 кадра и дошёл до cloud-200 без сообщений FAIL, но код завершения после потери tool-result не восстановлен: этот запуск отдельно не заявляется PASS. Существующий тест проверяет реальный QFileDialog, семантику выбора папки, accessibility, размеры команд и отмену без изменения draft. smoke_core exit0, cloud-picker-core.log. Нового установщика для этой правки пока нет.

Полный Qt77147 exit0: 9/9 PASS за 184.90с, build-qt/cloud-picker-full.log. Установленная версия и production-данные не менялись.
