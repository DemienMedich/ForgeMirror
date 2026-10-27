# ForgeMirror Qt 0.6.115 — проверенный кандидат

## Доставка — 27.10.2026

Уточнение фактической сверки Hub: apps.json перед доставкой указывал на build-qt/release-candidate-0.6.104/ForgeMirrorQt.exe, хотя registry уже указывал на установленный EXE. После обнаружения путь apps.json исправлен на C:/Users/mrdem/AppData/Local/Programs/ForgeMirror/ForgeMirrorQt.exe; повторная проверка точного равенства executable/version с registry прошла. Ниже «путь прежний» относится к установленному EXE и registry, а не к ошибочной исходной записи apps.json.

Обновляет исторический статус ниже: настоящий dist Setup установлен поверх0.6.114, exit0. Приложение было закрыто. Installed EXE0.6.115 побайтно совпал с проверенным пакетом; все90 ранее существовавших файлов рабочих данных сохранили SHA256. Запуск установленного EXE без среды разработки в PATH и с новым изолированным --storage-dir: exit0, stderr0; qwindows.dll присутствует. Рабочий профиль не открывался.

Доказательства: build-qt/delivery-0.6.115-e07255b4c8d74fbdad981794bab0a4cc (install.log, startup.err, приватный userdata-before.json и резервные записи Hub). Приватные перечни не включены в Git. Локальные apps.json в Z:\CPP\PharosHub\package\config и HKCU Software/Pharos/Hub/installed/forge-mirror согласованы с0.6.115; путь Qt EXE прежний. Другие записи Hub семантически неизменны. Живое нажатие кнопки Hub не проверялось, облачный manifest не публиковался.

27.10.2026. Канонический источник версии: VERSION. Изменение67e1988 убирает прототипное «Qt migration» из заголовка и «клиент переноса» из справки. Пути хранения и защитные ограничения не изменены. Рабочая установка и локальный Hub в этом проходе не обновлялись.

## Сборка и тесты

Предыдущая сборка остановилась до упаковки, её handle отсутствовал; пакет/Setup не существовали. Повторная сборка release-candidate-0.6.115-resumed-build.log завершилась exit0: Qt9/9 PASS215.12s, smoke_core OK. Отдельная проверка текста до смены версии: product-wording-native exit0 и product-wording-full Qt9/9 PASS192.77s.

Пакет Z:\CPP\ForgeMirror\build-qt\release-candidate-0.6.115 содержит31файл: EXE, runtime, плагины, qt.conf и штатная музыка. Без пользовательских баз и профилей; размеры/SHA256 в ForgeMirror-0.6.115-package.json.

## Установщик и жизненный цикл

Настоящий Setup: Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.115.exe.
SHA256: 0F37CEC711AFCD44B539E2246E27B84208A7516A20EB97728E5A254725A21C7E.
installer-0.6.115-build.log: exit0.

lifecycle-0.6.115.log: exit0. Доказательства: build-qt/lifecycle-0.6.115-0FCEF8ADD25E470784719170F8386A8F. Установка0.6.114→обновление0.6.115→удаление выполнены эквивалентными установщиками с отдельным тестовым AppId, не побайтово идентичными dist Setup.

ProductVersion, запись удаления и --version совпали с0.6.115. Реальный заголовок HWND: «ForgeMirror · 0.6.115», без migration. Запуск без среды разработки: exit0, stderr0; qwindows.dll присутствует, SHA25612577A7C4F2230BBEA53F279573D7E3D1A08D631197855481E1E1580F61BB877.

Удаление и helper: exit0; тестовый каталог и запись удаления исчезли. Внешний workspace marker сохранил SHA2567BB6463B30F9E301FED333CDF8960CA9497B602CCD8EEB46AE42693FDEA15A4D. Доставка пользователю остаётся отдельным шагом; общая эстетическая приёмка не объявляется завершённой.
