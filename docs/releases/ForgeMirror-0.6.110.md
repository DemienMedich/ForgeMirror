# ForgeMirror Qt 0.6.110

Изменения относительно0.6.109:

- Аналитика профиля: ширины заголовков и числовых колонок учитывают масштаб и сортировку; название сохраняет читаемый минимум при изменении ширины окна.
- Облако: колонка параметров занимает место по содержимому, значения получают свободную ширину. В узком окне используется внутренняя прокрутка.
- 3D: причина ошибки открытия модели сохраняется после повторного отображения страницы, вместо замены общим сообщением.

## Проверки исходников до смены версии

- Профиль: native profile-analytics-columns-native3.log exit0, масштабы90–200%, resize800→1440→800; Qt9/9 PASS188.74s, coreOK.
- Облако: cloud-scale-<scale>.log, семь масштабов exit0; cloud-columns-full.log Qt9/9 PASS188.87s, coreOK.
- 3D: model-error-after.log exit0, model-error-full.log Qt9/9 PASS189.02s, coreOK.
- Точная область визуальной проверки и ограничения: docs/design/UI_ACCEPTANCE_CURRENT.md. Эти результаты не означают завершения общей приёмки дизайна.

## Поставка

Канонический VERSION:0.6.110. Сборка38376 exit0, Qt9/9 PASS230.41s и coreOK; журнал build-qt/release-candidate-0.6.110-build.log.

- Setup: Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.110.exe; installer-0.6.110-build.log exit0.
- SHA256:9D3A3032073A3A645DC8BFA1165D2D71D6FD37083CFAE9E9C80EA18E7E632DC2.
- Пакет build-qt/release-candidate-0.6.110:31файл, набор путей совпадает с0.6.109. Длины и SHA256: ForgeMirror-0.6.110-package.json; пользовательских workspace/профилей в пакете нет.
- Lifecycle61160 exit0:0.6.106→0.6.110→удаление. Эквивалентные установщики с отдельным тестовым GUID, не побайтно тот же dist Setup.
- Evidence: build-qt/lifecycle-0.6.110-A20D87BE7B1B4B27B2489510C6B4A571; lifecycle-0.6.110.log.
- ProductVersion, FileVersion, запись удаления, --version и реальный заголовок окна совпадают:0.6.110. Запуск с Windows-only PATH:exit0/stderr0. qwindows.dll SHA256:12577A7C4F2230BBEA53F279573D7E3D1A08D631197855481E1E1580F61BB877.
- Uninstall/helper exit0; тестовые каталог/запись удаления удалены. Внешний marker сохранён:7BB6463B30F9E301FED333CDF8960CA9497B602CCD8EEB46AE42693FDEA15A4D.
- installed-0.6.110.png просмотрен: первый запуск без профиля читаем. Это не визуальная проверка всех заполненных страниц.

Рабочая установка и Hub остаются0.6.106; доставка пользователю ещё не выполнена, production-данные не менялись. Общая приёмка дизайна остаётся открытой.
