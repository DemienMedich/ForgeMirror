# Модули и whitelist ForgeMirror

## Ядро
- Профили (`*.ini`, `archive/*.ini`), навыки (`skills.txt`), правила (`meta/gameplay.ini`), UI (`meta/ui.ini`), облако (`meta/cloud.ini`), ярлыки (`meta/shortcuts.json`), пайплайн (`meta/pipeline.json`), задачи (`meta/tasks.json`), patch-notes (`meta/patch-notes/*.md`), ачивки (`achievements/*.json` + `achievements/icons/*.png`).
- Профессии (`meta/professions.txt`) — список профессий и метки категорий навыков.
- Все файлы хранятся в `%APPDATA%/ForgeMirror` (или выбранной storageDir); синхронизация использует тот же whitelist.

## Модули (feature flags)
- Отключение: `FORGEMIRROR_DISABLE_MODULES=tasks,pipeline,achievements,shortcuts,pomodoro,cloud,view3d,professions` (или `all`). UI и загрузка данных уважают флаги.
- Achievements: читает `achievements/*.json`, `achievements/icons/*.png`; tint иконок берётся из темы.
- Pipeline/Tasks: `meta/pipeline.json`, `meta/tasks.json`.
- Pomodoro: настройки в `meta/ui.ini` (`[pomodoro]`), звук — путь к файлу (WAV/MP3) в UI; при проблеме выводится статус.
- Cloud: `meta/cloud.ini`, `meta/manifest.ini`, облачный whitelist соответствует локальному.

## Очистка/health-check
- Кнопка «Очистить лишние файлы» в меню облака (админ) удаляет всё вне whitelist.
- Статус облака для админа показывает лишние файлы (с примерами), версию и штамп данных.

## Патч-ноуты
- Для каждой версии — `meta/patch-notes/<версия>.md`; синхронизируются вместе с данными.
