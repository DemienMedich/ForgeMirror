# ForgeMirror 0.6.102 — поставка проверена, установлен пользователю

Изменение: исправлена высота однострочной шапки при увеличении масштаба. Контролы при200% больше не обрезаются вертикальным viewport. Исходный фикс94aedfc.

До смены версии: подтверждённый BEFORE NAVIGATION exit1/viewport50; AFTER NAVIGATION offscreen exit0; Qt9/9 PASS200.86с; coreOK. Нативная проверка границ прошла и кадр200 просмотрен, но весь NAVIGATION exit1 на проверке hover (widgetAt null). Логи header-bounds-* и header-native-recheck сохранены. Не выдавать весь native-run за PASS.

Обычный запуск текущего build/Release на свежем workspace header-interactive-f7cbd4fa233b4b0b97a6a948a0c7f6bc: Computer Use выбрал единственное окно5245032/PID26072; активировал и просмотрел. Обновление выполняется, Tab переводит видимый фокус на облако. Чистый hover отдельно не доказан; API-кадры включают видимый курсор с эффектом. Штатно закрыт, PID отсутствует, stderr0. Qt runtime был в PATH — это не clean-PATH package proof.

Проверенные пути: Z:\CPP\ForgeMirror\build-qt\release-candidate-0.6.102 и Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.102.exe.

## Поставка

- Build27160 exit0; Qt9/9 PASS189.34с; smoke_core OK. Лог release-candidate-0.6.102-build.log.
- Инвентарь31 файла: ForgeMirror-0.6.102-package.json. Пользовательских профилей/баз/облачных настроек нет; Qt/CRT и platforms/qwindows.dll присутствуют.
- SHA256 настоящего Setup: AB3F8B881E00074D85BE2369339F537F70967E71556685D856C654D4CBD2C377.
- Lifecycle93063 exit0: отдельный GUID, install .101→update .102→uninstall, Windows-only PATH, EXE/окно/registry .102, startup0/stderr0, plugin присутствует; каталог/registry удалены, внешний marker сохранён. Это эквивалентные тестовые установщики, не изменение рабочей установки.
- Доказательства: build-qt/lifecycle-0.6.102-E28045B40D2B40ED818CD715C4CE9DD8, lifecycle-0.6.102-run.log. Кадр установленного приложения просмотрен.

## Замена пользовательской установки по явному запросу 05.10.2026

- Ветка ImGui codex/pre-qt-2026-08-28 сохранена на origin:7306152c603ff8007200f64e63c4188510d55588, SHA сверен.
- Legacy0.5.27 Z:\Soft\ForgeMirror штатно удалён (exit0). Старой папки и старой uninstall-записи больше нет. Исходники репозитория и ветки не удалялись.
- Перед удалением сохранены248 файлов data в C:\Users\mrdem\AppData\Local\ForgeMirrorMigrationBackups\imgui-retirement-20261005-132924\legacy-install-data; копия проверена SHA256. Манифест хранится рядом, не публикуется в git.
- Qt0.6.87 обновлён настоящим Setup до0.6.102 в C:\Users\mrdem\AppData\Local\Programs\ForgeMirror, exit0. Установленный EXE совпадает SHA256 с проверенным пакетом; registry0.6.102.
- Ярлыки Desktop\Программы\ForgeMirror.lnk и Desktop\ForgeMirror Qt.lnk перенаправлены на установленный ForgeMirrorQt.exe, без аргументов; start-menu восстановлен установщиком.
- Хеши43 файлов прежнего профиля и47 файлов Qt workspace не изменились. Рабочие данные не объединялись и не перезаписывались.
- Установленный EXE на отдельном тестовом workspace, Windows-only PATH: smoke exit0/stderr0. Production workspace для проверки не открывался. Логи imgui-uninstall.log, production-qt-update.log, installed-qt-check.* находятся в build-qt.
