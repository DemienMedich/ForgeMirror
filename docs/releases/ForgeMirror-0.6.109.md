# ForgeMirror Qt 0.6.109

Исправлена читаемость таблицы ярлыков: минимальная ширина пути и названия учитывает масштаб, статус получает ширину оформленной ячейки Qt. В широком окне путь занимает свободное место, в узком используется внутренняя прокрутка. Запуск ярлыков и права не изменены.

## Подтверждено до смены версии

- Native EMPTY_LISTS exit0, обе роли, семь масштабов, resize800→1440→800: build-qt/shortcut-columns-final.log.
- Полный Qt9/9 PASS201.28s; coreOK: shortcut-columns-final-full.log и shortcut-columns-final-core.log.
- Кадры native-shortcut-columns-after-100/200.png: полный статус, полезная ширина пути. При200% показан правый край таблицы, не все колонки одновременно.

## Поставка и проверка

- Setup: Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.109.exe.
- SHA256:1FC09F17419AC6C1CE623A2E66CD4AD1D84E57ACAC7150EA1746F8160801DF04.
- Пакет build-qt/release-candidate-0.6.109:31файл, состав путей совпадает с0.6.108; ForgeMirror-0.6.109-package.json содержит длины/хеши. Только приложение, зависимости, plugins, qt.conf и3штатныхMP3.
- Сборка77989 exit0; Qt9/9 PASS198.01s, coreOK. release-candidate-0.6.109-build.log. Настоящий dist Setup собран exit0, installer-0.6.109-build.log.
- Lifecycle75689 exit0: install0.6.106→update0.6.109→uninstall. Проверены эквивалентные Setup с отдельным тестовым GUID, не побайтно тот же dist Setup.
- Доказательства: build-qt/lifecycle-0.6.109-427A668FA48A447CBF377B6C3C54C071, lifecycle-0.6.109.log.
- ProductVersion/UninstallDisplayVersion/--version/реальный заголовок окна:0.6.109. Windows-only PATH: startup0/stderr0; qwindows SHA25612577A7C4F2230BBEA53F279573D7E3D1A08D631197855481E1E1580F61BB877.
- Удаление0/helper0, каталог и запись удаления исчезли; внешний marker неизменен:7BB6463B30F9E301FED333CDF8960CA9497B602CCD8EEB46AE42693FDEA15A4D.
- installed-0.6.109.png просмотрен: первый запуск без профиля читаем. Это не проверка всех заполненных страниц.

Рабочая установка и Hub остаются0.6.106, доставка пользователю ещё не выполнена. Production данные не менялись. Общая приёмка UI остаётся открытой; следующий адресный блок — модель и ошибки3D.
