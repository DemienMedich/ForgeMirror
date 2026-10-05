# ForgeMirror 0.6.104 — проверенная поставка

Исходное изменение cd58826: сокращена повторяющаяся нижняя подпись сравнения облачных файлов, полное предупреждение остаётся над вкладками, в accessibility и tooltip. Изменение поведения операций не вносилось. Снимки до/после: ../design/UI_CONFLICT_FOOTER.md.

До повышения версии: native DECISION_FORMS PASS404/83/20 на семи масштабах; полный Qt9/9 PASS183.66с, smoke_core OK. Эти результаты не заменяют проверку новой поставки.

Текущая сборка: сессия55796; build-qt/release-candidate-0.6.104-build.log. Плановый пакет build-qt/release-candidate-0.6.104; плановый установщик dist/ForgeMirrorSetup_0.6.104.exe. Сборка55796 завершилась exit0. Итоговые результаты ниже.

Установленная версия и Hub остаются на Qt0.6.102. Локальный каталог Hub и override HKCU/Software/Pharos/Hub/installed/forge-mirror исправлены с ImGui на установленный ForgeMirrorQt.exe; runtime-клик Hub ещё не подтверждён. Пользовательские данные не меняются.


## Результаты поставки

- Qt9/9 PASS191.37с, smoke_core OK. Пакет содержит31 файл (ForgeMirror-0.6.104-package.json), Qt/CRT/platforms/qwindows.dll и3 штатных музыкальных файла, без пользовательских профилей/баз/секретов.
- Настоящий Setup: Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.104.exe. SHA256:87F579774DC4376FC01D773A6D372CEF9469669CE6BCEB65D968A725A68AC3A3.
- Lifecycle61978 exit0: эквивалентные установщики с отдельным test GUID, install0.6.103→update0.6.104→uninstall. Windows-only PATH; version command, EXE, window title и uninstall registry0.6.104; startup0/stderr0; platformplugin присутствует.
- Uninstall/helper exit0; тестовые каталог и registry удалены, внешний marker неизменен. Кадр установленного первого запуска просмотрен. Эти testSetup имеют иной SHA из-за отдельной идентичности.
- Доказательства: build-qt/lifecycle-0.6.104-97A7D92C20864F6EB3BCC2ED13930A68 и lifecycle-0.6.104-run.log.
- Пользователь подтвердил плавность навигации и стрелки «Дополнительно» на установленной версии; это не автоматическая приёмка остальных состояний. Общая UI-цель пока не закрыта.
