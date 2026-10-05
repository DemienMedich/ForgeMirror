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
