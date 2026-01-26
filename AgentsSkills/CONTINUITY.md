# CONTINUITY

## Текущее состояние
- Ветка: Beta. Текущая версия 0.3.12 (откат UI/UX изменений за последние два дня; восстановлены AgentsSkills UI-гайды; сохранены текущие модули); добавление опыта переведено на оценки 1-5 (автодолей).
- Синхронизация: whitelist профилей/skills/tasks/pipeline/gameplay/achievements/icons/meta/patch-notes, кнопка очистки лишнего (только админ).
- Инсталлер: сборка через `installer/build-installer.ps1` (требуется Inno Setup 6 и ISCC.exe).
- Smoke_core проверяет: загрузку/сохранение профиля и ранга, gameplay.ini, tasks/pipeline файлы, whitelist.

## Команды
- Сборка: `cmake --build build --config Release`
- GUI: `cmake --build build-gui --config Release --target JobSkillGui`
- Smoke-тест: `cmake --build build --config Release --target smoke_core` затем `build/Release/smoke_core.exe`
- Инсталлер: `powershell -NoProfile -ExecutionPolicy Bypass -File installer/build-installer.ps1 -Configuration Release -IsccPath "C:\\Users\\mrdem\\AppData\\Local\\Programs\\Inno Setup 6\\ISCC.exe"`

## План / бэклог
- MyIdeas:
  - [x] Удаление задач в списке задач с откатом начисленного XP и счётчиков.
  - [x] Группировка навыков по категориям (категории в skills.txt, фильтр/группировка в каталоге).
  - [x] Профессии: CRUD/админ в отдельном модуле; профили только отображают название; навыки привязываются к профессии.
- Стабильность ядра / модульность: минимальное ядро (профили, навыки, XP/уровни, правила, storage API); feature-флаги модулей; единые функции чтения/записи с BOM и валидацией.
- Сиды/профили: не подтягивать удалённые сиды; whitelist профилей для seed/инсталлятора; все данные профиля в одном месте.
- Обновления/документация: patch-notes на каждую версию в `data/meta/patch-notes`; README/UPDATE_GUIDE для пользователей/облака.
- Добавление опыта: оценки 1-5 и авторасчёт долей.