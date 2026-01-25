# Обновление ForgeMirror

## Где лежат данные
- Основное хранилище: `%APPDATA%/ForgeMirror`
- Структура: `*.ini` (профили), `archive/*.ini`, `skills.txt`, `meta/pipeline.json`, `meta/tasks.json`, `meta/gameplay.ini`, `meta/shortcuts.json`, `meta/ui.ini`, `meta/cloud.ini`, `achievements/*.json`, `achievements/icons/*.png`, `meta/patch-notes/*.md`
- Профессии: `meta/professions.txt` (копируется при push/pull, чистится по whitelist).

## Обновление через установщик
1. Собрать установщик:  
   `powershell -NoProfile -ExecutionPolicy Bypass -File installer/build-installer.ps1 -Configuration Release -IsccPath "C:\Users\mrdem\AppData\Local\Programs\Inno Setup 6\ISCC.exe"`
2. Запустить `ForgeMirrorSetup_<версия>.exe` (per-user, без UAC).
3. Данные в `%APPDATA%/ForgeMirror` не трогаются, поверх ставится новый GUI.

## Обновление через облако
- Админ: Push (`Облако -> Выгрузить`) выгружает профили, навыки, tasks/pipeline/gameplay, ачивки/иконки, patch-notes; удаляет лишние файлы по whitelist.
- Пользователь: Pull (`Облако -> Обновить`) получает актуальные данные.
- В меню облака админ видит предупреждения о лишних файлах (health-check).

## Создание версии / патч-ноут
- APP_VERSION обновлять в `CMakeLists.txt` при заметных изменениях.
- Для каждой версии добавлять файл `data/meta/patch-notes/<версия>.md`; папка участвует в синке.

## Отключение модулей (для диагностики)
- Env `FORGEMIRROR_DISABLE_MODULES=tasks,pipeline,achievements,shortcuts,pomodoro,cloud,view3d` (через запятую или `all`) отключает модули. UI/логика ядра должны работать без падений.
