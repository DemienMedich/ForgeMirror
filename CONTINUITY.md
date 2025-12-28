Goal (incl. success criteria):
- Keep JobSkill CLI/GUI stable while fixing gameplay/UX issues (Russian locale-friendly text, correct XP logic, editable skill catalog); builds in Release must succeed.

Constraints/Assumptions:
- Locale/formatting: force classic/UTF-8 to avoid garbled numbers/strings; GUI/CLI must stay Russian-friendly.
- Degradation system currently disabled (kDecayEnabled=false) and should stay off unless explicitly re-enabled.
- Skill catalog persists to data/skills.txt; profiles stored in data/*.ini via FileStorage.
- Achievements durations are specified in days only; countdown timer still displays hours/minutes.
- No automated tests for now (per user request).

Key decisions:
- Disabled category decay/buffer recomputation (both CLI and GUI) via kDecayEnabled=false.
- Gameplay rules parsing uses classic locale and sanitized numbers; penalties remain active.
- GUI/CLI logs use classic locale to avoid non-ASCII separators.
- GUI supports adding/removing skills (persists catalog and cleans profiles on delete).
- Skill IDs are stable internal identifiers; GUI/CLI show display names (incl. Cyrillic).

State:
- Builds: Release targets JobSkill.exe and JobSkillGui.exe compile clean.
- Storage: FileStorage now writes/reads numbers locale-independently; recoveryTasks preserved across restarts.
- Documentation: Added docs/USER_GUIDE.md (RU) describing gameplay rules, XP system, skill catalog management, storage, and CLI/GUI usage.
- Access control: Admin profile seeded as "Admin" (admin flag persisted); admin password `admin123` required for full access. CLI/GUI default to read-only until admin login.
- Localization: Gameplay rules and GUI controls translated to Russian; user-facing statuses/logs/messages now predominantly RU (XP labels remain).
- Achievements: stored per-profile in data/achievements/*.json with bonus applied to skill XP; GUI shows icons + tooltip (expiry/remaining time); icon textures cached in GUI; JSON parsing is now order-agnostic.
- Skill catalog: admin can add/remove skills and rename/edit descriptions; catalog now stores stable skill IDs with display names; rename uses confirmation before merging XP.
- UI customization: added `data/meta/ui.ini` with theme/style controls, per-window background textures from `data/ui/backgrounds`, and a 3D preview window that loads `.obj`/`.fbx` from `data/models` (wireframe + orbit controls).
- UI recovery: clamps invalid UI settings and enforces min window sizes; `F10` resets UI settings + clears layout.
- Workspace: main menu now toggles visibility of auxiliary windows; profile stays always visible.

Done:
- Fixed level progress bar math (uses total needed XP).
- Applied gameplay bonuses/penalties from saved rules.
- Stopped resetting levels when rules saved.
- Prevented recoveryTasks reset; sanitized FileStorage numeric parsing.
- Disabled degradation system.
- Added skill catalog add/delete UI and catalog removal from profiles.
- Normalized log formatting to ASCII/classic locale.
- Added admin gating: CLI command `admin` to toggle admin mode; non-admin users can только смотреть. GUI adds admin login/logout, disables mutations (profiles, XP, rules, skills) until authenticated.
- Added achievements system with JSON persistence, skill XP bonus, admin issue/edit/delete UI, profile icon grid with tooltip timer; fixed icon loading.
- Added admin-only skill rename/description edit in GUI with merge confirmation and XP merge logic.
- Switched skill persistence to stable IDs with display names; SyncProfileWithCatalog migrates legacy name-based data.
- Achievements JSON now escapes strings; parser decodes `\\`/`\u` and keeps Windows paths intact.
- CLI now applies achievement XP bonuses (task/addxp) and reports bonus percent.
- GUI “Последние действия” uses configured repeat/recovery percentages.
- Profile delete/ID normalization cleans up achievement JSON files.
- UI settings window with theme/color/spacing controls and per-window backgrounds; saved to `data/meta/ui.ini`.
- 3D preview window with OBJ/FBX loading (wireframe) and default cube fallback.
- Added background tiling option for UI backgrounds; split 3D view and 3D settings into separate windows; added borderless-window toggle, fullscreen mode (F11), and Alt+drag window movement for borderless mode.
- Added admin-only app log panel with severity filtering; key operations now emit structured logs (profiles, rules, catalog, achievements, UI settings).
- UI settings now include presentation/compact presets; app logs support text search and export to `data/meta/logs`.
- Added admin-only statistics panel (summary KPIs + top profiles by level/XP) in the workspace tabs.
- Profile view now includes a "Состояние" block with last activity, recovery status, and categories needing attention.
- Added category mini-report in profile (progress bars), admin export reports (TXT/CSV) to `data/meta/reports`, and skill table filters by weight category/range with hover tooltips.
- Admin stats now include rank distribution and least-active profiles list (based on last activity + recovery tasks).
- Admin stats tables are now clickable to jump to a profile; centralized rank-selection sync added.
- Added admin stats CSV export and profile activity log filtering/export in the profile view.
- Added profile indicators (recovery, categories below 10/10, stale activity), copy-ID button, and inactivity threshold slider in admin stats.
- Admin stats now support ID/name filtering and archive toggle; exports respect filters. Activity export respects the current filter.
- Added achievements KPIs in admin stats, profile KPI now shows active/total achievements, and a "Топ навыков" block in profile.
- Admin stats KPI now includes count of profiles with active recovery (penalty) tasks.
- Profile overview now shows progress to the next rank; profile list shows filtered count.
- "Топ навыков" now supports sorting by XP/level/weight.
- Admin stats now track profiles without activity/achievements and include a top achievements table.
- Admin stats now include average category scores across profiles.
- Achievements section now lists expiring-soon badges; profile skill table shows total XP column.
- Achievements section now supports filtering and hiding expired items with counts.
- Add Experience modal now previews global XP after repeat/recovery penalties.
- Added copy-name button in profile details and a "profiles on recovery" table in admin stats.
- Added exit button in main menu.
- F10 toggles borderless mode; Ctrl+F10 resets UI/layout.
- Added window controls (fullscreen/borderless) in main menu; removed auto-fullscreen on borderless.
- Added reset buttons for profile/skill/activity/achievement/admin/log filters.
- Admin stats now support auto-refresh with configurable interval.
- Logs panel now has a compact view toggle.
- Add Experience modal now has a quick "Равномерно" distribution button and color-coded 100% indicator.
- Add Experience modal now supports skill filtering and sorting, with visible count.
- Profile panel now uses tabs (overview/achievements/skills/chart/activity) to reduce clutter.
- Expired achievement icons render dimmer in the profile grid.
- Achievements grid now uses a fixed column table for consistent rows/spacing.
- Profile summary now labels the level progress bar and shows top-skill count.
- Admin stats toolbar is split into refresh/status and filter/export rows for cleaner layout.
- Skill catalog list now supports sort by name/weight with a filter reset and consistent shown counts.
- Profile indicators are now compacted into a status table with a signal count.
- Admin stats tables now use tighter cell padding and unified top table heights.
- Admin stats inactive/recovery tables now use the same fixed height with scroll for consistency.
- Skill catalog now includes a weight-category filter.
- Profile "Состояние" block uses a compact 3-column table with colored status markers and tooltip details.
- Profile list now filters by a compact "Все/Активные/Архив" selector.
- Admin login shows current keyboard layout under the password field.
- Skill achievements list now has status/filter controls and shown count.
- Pipeline list now has a quick filter with shown count.
- Logs panel now supports a source filter dropdown.
- Admin stats now support filtering by rank.
- Activity log now shows a visible count alongside the filter.
- Icon picker now supports filtering by name with a visible count.
- 3D model picker and UI background picker now support name filters with visible counts.
- Add Experience skill list now shows the visible count near the filter (before the table).
- Add Experience now shows an empty-state message when no skills match the filter.
- Admin stats inactive/recovery sections now show "Показано X из Y" counts.
- Workspace tabs now auto-focus newly opened tabs.
- UI settings now expose all ImGui colors grouped by category, and support user presets.
- Profile now shows the 5 most recent achievements centered under the level progress bar.
- UI settings auto-save on exit to preserve background alpha and other changes.
- Fixed float parsing to use classic locale for UI settings, preventing background alpha reset on reload.
- Main menu now includes a compact "Быстрые действия" panel (create/add XP/archive/restore/stats) plus a separate service row.
- Skill catalog now uses a two-panel layout with a right-side editor and optional weight-category grouping.
- Profile summary now highlights level/rank/total XP/active bonus; last selected profile tab is persisted in UI settings.
- Logs/admin stats filters now persist between sessions and use a unified "Фильтры" header.
- UI settings now include theme presets, a theme reset button, and quick background application to all windows.
- Added fixed left navigation sidebar with hotkeys F1-F6 for primary sections (profile/catalog/pipeline/rules/stats/logs).
- Split panel rendering code into `gui/GuiPanels.inc` to reduce `gui/GuiApp.cpp` size and isolate UI panels.
- Refactored GUI filter buffers into `UiFilters` (stored in `GuiState.filters`) with syncing to persistent UI settings.
- Extracted UI settings/preset load-save and window-mode helpers into `gui/GuiUiSettings.inc` to shrink `gui/GuiApp.cpp`.
- Split `GuiState` into `GuiDataState` and `GuiRuntimeState` (via public inheritance) to separate loaded data vs UI runtime without changing call sites.
- Extracted log/text/format helpers into `gui/GuiTextUtils.inc` to declutter `gui/GuiApp.cpp`.
- Extracted icon cache/chooser utilities into `gui/GuiAssets.inc` and 3D mesh helpers into `gui/GuiMesh.inc`.
- Extracted reporting/export helpers (logs/profile/admin CSV/TXT) into `gui/GuiReports.inc`.
- Extracted admin statistics refresh logic into `gui/GuiAdminStats.inc`.
- Extracted UI window helpers (backgrounds, window mode toggles, visibility clamps) into `gui/GuiUiHelpers.inc`.
- Extracted profile selection/refresh helpers into `gui/GuiProfileOps.inc`.
- Extracted UI status/log helper routines (SetStatus, KPI cards, log banner) into `gui/GuiStatus.inc`.
- Extracted XP/skill helper routines (XP totals, distribution balancing, category labels) into `gui/GuiXpUtils.inc`.
- Moved XP share balancing helpers (IncreaseOthers/BalancePercentages/AdjustSkillShare) into `gui/GuiXpUtils.inc`.
- Extracted skill radar chart rendering into `gui/GuiCharts.inc`.
- Extracted rank definitions/helpers into `gui/GuiRanks.inc`.
- Extracted pipeline step descriptions into `gui/GuiPipeline.inc`.
- Extracted UI window metadata, settings, and color grouping helpers into `gui/GuiUiData.inc`.
- Extracted core GUI types (profile wrapper, log types, confirmation enums, pipeline step struct) into `gui/GuiTypes.inc`.
- Extracted admin stats data structures into `gui/GuiAdminTypes.inc`.
- Extracted storage helpers (asset search + profile list load) into `gui/GuiStorageUtils.inc`.
- Extracted GUI state structs into `gui/GuiState.inc`.
- Extracted startup helpers (locale + font loading) into `gui/GuiStartup.inc`.
- Moved string normalization helpers (skill name + trim) into `gui/GuiTextUtils.inc`.
- Extracted window-creation hints into `gui/GuiWindowInit.inc`.
- Extracted GUI state initialization into `gui/GuiStateInit.inc`.
- Added layout-path setup helper in `gui/GuiStartup.inc` to keep main init compact.
- Added ImGui backend init helper in `gui/GuiStartup.inc`.
- Added ImGui context init helper in `gui/GuiStartup.inc`.
- `RunGuiLoop` now consistently uses `IJobStorage&` (no pointer deref) and reformatted `gui/GuiMainLoop.inc` for readability.
- Профильные вкладки: убран постоянный `SetSelected`, добавлен одноразовый `profileTabRequest` для корректного переключения без дерганья; сброс UI теперь пересинхронизирует вкладку.
- Профильный интерфейс вынесен в `DrawProfilePanel` в `gui/GuiPanels.inc`, чтобы разгрузить `gui/GuiMainLoop.inc`.
- Блок рабочего окна вынесен в `DrawWorkspacePanel` в `gui/GuiPanels.inc` (включая вкладки каталога/пайплайна/настроек).
- Модальные окна (админ‑логин, каталог, XP, создание/подтверждение) вынесены в `gui/GuiModals.inc`.
- Обновление UI‑кадра (горячие клавиши, темы, окно) вынесено в `UpdateUiFrame` в `gui/GuiHeader.inc`.
- Навигация и главное меню вынесены в `DrawNavigationPanel`/`DrawMainMenuPanel` в `gui/GuiPanels.inc`.
- Финализация кадра (закрытие, render, swap) вынесена в `FinalizeFrame` в `gui/GuiRender.inc`.
- Панель правил вынесена в `gui/GuiRulesPanel.inc` (подключена после зависимостей).
- Панель настроек интерфейса вынесена в `gui/GuiUiSettingsPanel.inc`.
- Панель каталога навыков вынесена в `gui/GuiSkillCatalogPanel.inc`.
- Панель логов вынесена в `gui/GuiLogsPanel.inc`.
- Панель статистики администратора вынесена в `gui/GuiAdminStatsPanel.inc`.
- Панели 3D просмотра/настроек вынесены в `gui/GuiView3dPanels.inc`.
- Панель профиля вынесена в `gui/GuiProfilePanel.inc`.
- Рабочее окно (таб‑контейнер) вынесено в `gui/GuiWorkspacePanel.inc`.
- Навигация и главное меню вынесены в `gui/GuiNavigationPanel.inc` и `gui/GuiMainMenuPanel.inc`.
- Панель пайплайна вынесена в `gui/GuiPipelinePanel.inc`; вспомогательные меню‑хелперы — в `gui/GuiMenuHelpers.inc`.
- Модальные окна разделены по файлам: `gui/GuiAdminModal.inc`, `gui/GuiSkillModals.inc`, `gui/GuiXpModal.inc`, `gui/GuiProfileModals.inc`.
- Удалены пустые заглушки `gui/GuiPanels.inc` и `gui/GuiModals.inc` после разбиения.
- `UpdateUiFrame` разбит на небольшие хелперы в `gui/GuiHeader.inc` (hotkeys/theme/window state/guards).
- Moved storage/bootstrap wiring into `InitStorageContext` in `gui/GuiStorageUtils.inc`.
- Started service-layer extraction: created `GuiActions` for profile/rules/skill/achievement operations and wired GUI to use it.
- Added `SanitizeGameplayConfig` for centralized validation; used on load/save and GUI rule saves.
- Profile UI: added "Обзор" section and grouped achievements/skills/chart/activity into collapsible sections for cleaner navigation.
- Workspace UI: consolidated tools into a single tabbed "Рабочее окно"; added profile/skill search filters (incl. profile skill search), KPI cards in profile overview, uniform toolbar layout in main menu, a scrollable activity section, and a table-based profile skills view.
- Skill catalog list now uses a table with weight column and filtered result count.
- Profile skills table supports sorting (name/level/XP/weight).
- Profile list: added archive toggle and sorting by ID/name with filter-aware display.

Now:
- Continuing UX/professional polish in GUI (admin analytics panel added; awaiting next refinements).

Next:
- TBD based on user request (e.g., refine catalog UX, re-enable decay with config, further localization).

Open questions:
- Need confirmation if decay should ever return and under what rules (UNCONFIRMED).
- Should achievement XP bonuses apply in CLI flows (`task`/`addxp`) the same way as GUI?
- Should repeat/recovery penalties affect skill XP or only global XP?
- When adding new skills to the catalog, should existing profiles auto-add them at level 1/0 XP?
- When deleting a profile, should its achievements JSON be removed too?

UI customization notes:
- User wants all three: theme/style switching, background/plates customization, and ability to show 3D objects.

Working set (files/ids/commands):
- gui/GuiApp.cpp, src/SkillCatalog.cpp, include/SkillCatalog.h, src/FileStorage.cpp, main.cpp; data/skills.txt; builds via `cmake --build build --config Release` and `cmake --build build-gui --config Release --target JobSkillGui`.
