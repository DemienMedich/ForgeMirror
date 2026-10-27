# ForgeMirror Qt0.6.117 — установленная версия

27.10.2026. Каноническая версия VERSION. Изменение4dd6ff6 добавляет штатные раскрываемые подробности в Audit/Logs для чтения полного сообщения с клавиатуры. Палитра, формат данных и основной список сохранены. Native-проверка Space/open/full plainText/Space/close и просмотр800px/100% описаны в UI_ACCEPTANCE_CURRENT.

## Сборка и пакет

release-candidate-0.6.117-build.log: exit0, Qt9/9 PASS220.29s, smoke_core OK. Перед сменой версии journal-details-full: Qt9/9 PASS223.49s. Пакет build-qt/release-candidate-0.6.117 содержит31файл: приложение, runtime/Qt-плагины, qt.conf и штатную музыку, без пользовательских данных. Инвентарь: ForgeMirror-0.6.117-package.json.

Настоящий установщик: Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.117.exe.

SHA256: 67B44142D06053141C90A7E4CA38178A20F15DC97FF96B3BFC2EB87DD8A136E3.

## Изолированный жизненный цикл

installer-0.6.117-build.log и lifecycle-0.6.117.log exit0. Установка0.6.116→обновление0.6.117→удаление проверены эквивалентными Setup с отдельным тестовым AppId, не побайтово идентичными dist Setup. Доказательства: build-qt/lifecycle-0.6.117-5DF22B8FF355446780C1F7F85E893DDB.

ProductVersion, uninstall DisplayVersion, --version и настоящий HWND-заголовок совпали с0.6.117. Clean-PATH запуск exit0/stderr0. qwindows.dll SHA25612577A7C4F2230BBEA53F279573D7E3D1A08D631197855481E1E1580F61BB877. Uninstall/helper exit0; тестовый каталог и запись удаления исчезли; внешний workspace marker сохранил SHA2567BB6463B30F9E301FED333CDF8960CA9497B602CCD8EEB46AE42693FDEA15A4D.

## Доставка

Настоящий dist Setup установлен поверх0.6.116, exit0; приложение перед этим не работало. Installed EXE совпал с пакетом поSHA256. Все90 ранее существовавших файлов рабочих данных сохранилиSHA256. Это не утверждение об отсутствии новых файлов.

Доказательства: build-qt/delivery-0.6.117-7d776c70089b4013a5e358a2335beeea. Installed EXE запущен с PATH толькоWindows/System32, без QT_PLUGIN_PATH/QT_QPA_PLATFORM_PLUGIN_PATH и с новым тестовым --storage-dir: exit0/stderr0; qwindows.dll присутствует. Рабочий профиль не открывался.

Локальные apps.json в Z:\CPP\PharosHub\package\config и HKCU Software/Pharos/Hub/installed/forge-mirror согласованы:0.6.117, C:/Users/mrdem/AppData/Local/Programs/ForgeMirror/ForgeMirrorQt.exe. Другие приложения Hub семантически не менялись. Живое нажатие Hub не проверялось; облачный manifest не публиковался. Приватные перечни/резервные записи находятся только в delivery-папке.

Это завершённая поставка данного изменения, не автоматическое закрытие общей эстетической приёмки.

## Публикация в PharosHub — 27.10.2026

По прямому запросу пользователя manifest общей папки Z:\Yandex.Disk\PharosHub\releases\forge-mirror обновлён с 0.6.104 до 0.6.117 штатным publish-release.ps1. Executable: ForgeMirrorQt.exe. Исходные 31 файла сверены с ForgeMirror-0.6.117-package.json; после публикации повторно проверены все SHA-256. Старый payload сохранён. Setup скопирован в installers/forge-mirror/ForgeMirrorSetup_0.6.117.exe, рядом .sha256; хеш совпадает с указанным выше.

HubInstallTests --real-release выполнил установку опубликованного пакета с изолированными настройками (exit0). Установленная тестовая копия совпала с исходной по SHA-256 и ProductVersion=0.6.117; запуск с системным PATH, без QT_PLUGIN_PATH/QT_QPA_PLATFORM_PLUGIN_PATH, новым --storage-dir и --smoke-test: exit0, stderr0. Доказательства: build-qt/hub-publication-0.6.117 (предыдущий manifest, install.log, startup-error.log, result.json). Пользовательская установка и данные этой проверкой не менялись.

Проверена публикация в локальной синхронизируемой папке Яндекс.Диска; завершение загрузки на сервер и получение на другом компьютере не подтверждались. В Hub требуется обновить список. Переустановка самого Hub не требуется.
