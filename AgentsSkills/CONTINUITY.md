Goals (актуально)
- Стабильность 0.2.96: фикс рангов, техно-стекло тема, безрамочное перетаскивание.
- Облачная синхронизация: профили/ачивки/иконки, skills.txt, tasks.json, pipeline.json, gameplay.ini; очистка лишних файлов в облаке.
- Установщик: сборка с актуальной версией, только GUI, иконка FM; рабочая команда сборки под Inno 6.
- Подготовка к бете: проверить профили/темы между портативом и установщиком, синк статусы, удалить лишние данные из облака при push.

Constraints
- Ответы на русском, кратко.
- Без градиентов; плашка окна = фон окна.
- После UI-правок запускать `cmake --build build --config Release` и `cmake --build build-gui --config Release --target JobSkillGui`.
- Контроль версий; CONTINUITY.md держать непременно в репо, но можно не коммитить вместе с кодом.

State (текущее)
- Версия: APP_VERSION=0.2.96 (CMake). Установщик берёт версию из CMake, fallback в .iss также 0.2.96; проверка AppVersion проста (`#if AppVersion == "0.0.0"`). build-installer.ps1 умеет `-IsccPath` и ищет ISCC в Program Files/x86.
- Облако: добавлены правила (meta/gameplay.ini) в pull/push; удаление orphan файлов (skills/pipeline/tasks/gameplay/profiles/achievements). Штамп данных учитывает правила.
- UI: drag окна в безрамочном режиме через верхнюю панель; ранги в тексте и бейдже совпадают.
- Seed: иконка FM подключена; installer ico/rc добавлены; дист/ игнорится.
- Seed-профили: whitelist пуст (копирование сидов выключено), из `data/` удалены 0002/0003 (Anastasiya/Roman), чтобы установщик не тянул их.
- Модули: добавлен env-флаг `FORGEMIRROR_DISABLE_MODULES` (или JOBSKILL_...) для отключения модулей (tasks, pipeline, achievements, shortcuts, pomodoro/timer, cloud/sync, view3d); навигация/обновления данных уважают эти флаги.
- Помодоро: обновление состояния выполняется только при включённом модуле; таймер-заглушка в навигации показывает, что модуль отключён.

Done recently
- Исправлен расчёт текстового ранга (BuildRank +1) — не отстаёт от бейджа.
- Inno: упрощённая проверка версии (без StrCmp), поддержка явного `-IsccPath`.

Next
- Проверить/устранить дубли уровней/рангов в UI (визуальная вёрстка).
- Пройтись по синку: пустая папка облака, лишние файлы, статусы.
- Подготовить инструкцию обновления через облако/установщик для пользователей.

Команды
- Сборка: `cmake --build build --config Release`  
          `cmake --build build-gui --config Release --target JobSkillGui`
- Установщик: `powershell -NoProfile -ExecutionPolicy Bypass -File installer/build-installer.ps1 -Configuration Release -IsccPath "C:\Users\mrdem\AppData\Local\Programs\Inno Setup 6\ISCC.exe"`
