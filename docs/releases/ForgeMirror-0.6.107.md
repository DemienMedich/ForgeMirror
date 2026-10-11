# ForgeMirror Qt 0.6.107

Исправлена читаемость столбцов задач профиля при увеличенном масштабе: ширины учитывают UI scale, заголовки, статусы и полные сроки; все поля доступны внутренней горизонтальной прокруткой. Палитра/операции/данные не менялись.

## Поставка и проверка

- Setup: Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.107.exe
- SHA256: 4870771E446E76BFC1F621D004FC00B951016E1B255D2C41F9E849916CDB3240
- Пакет: build-qt/release-candidate-0.6.107;31файл, состав путей совпадает с .106. Инвентаризация: ForgeMirror-0.6.107-package.json; только EXE/Qt/CRT/plugins/qt.conf/три штатных MP3.
- build93723 exit0: Qt9/9 PASS183.24с, coreOK, deployment завершён. Лог release-candidate-0.6.107-build.log.
- Настоящий dist Setup собран exit0, installer-0.6.107-build.log.
- lifecycle90206 exit0: эквивалентные тестовые Setup с отдельным GUID, install.106→update.107→uninstall. Это не побайтно тот же dist Setup.
- Доказательства: build-qt/lifecycle-0.6.107-0EC8EFAA472A4206B451B9BBB48A6DE2; лог lifecycle-0.6.107.log.
- InstalledProductVersion/UninstallDisplayVersion/--version/title:0.6.107. Windows-only PATH, startup0/stderr0; qwindows SHA25612577A7C4F2230BBEA53F279573D7E3D1A08D631197855481E1E1580F61BB877.
- Uninstall0/helper0, каталог и запись удаления удалены; внешний marker сохранён, SHA2567BB6463B30F9E301FED333CDF8960CA9497B602CCD8EEB46AE42693FDEA15A4D.
- installed-0.6.107.png просмотрен: корректный первый запуск без профиля; не доказательство заполненных страниц.

## Дизайн и границы

Регрессия до изменения падала на заголовках/сроках; после native PROFILE_MODES прошёл. Левый край100/200 и правый край200 просмотрены; кадры profile-task-columns-after* в docs/design/visual-audit/2026-10-12. Все столбцы не обязаны одновременно помещаться при200%. Подробнее UI_PROFILE_TASK_COLUMNS.md.

Рабочая установка и локальный Hub остаются0.6.106: доставка .107 ещё не выполнена. Production данные не менялись. Общая приёмка дизайна остаётся открытой.