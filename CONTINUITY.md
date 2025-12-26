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
- Borderless mode now defaults to fullscreen when enabled or loaded from settings.
- F10 toggles borderless mode; Ctrl+F10 resets UI/layout.
- Added window controls (fullscreen/borderless) in main menu; removed auto-fullscreen on borderless.
- Added reset buttons for profile/skill/activity/achievement/admin/log filters.
- Admin stats now support auto-refresh with configurable interval.
- Logs panel now has a compact view toggle.
- Add Experience modal now has a quick "Равномерно" distribution button and color-coded 100% indicator.
- Add Experience modal now supports skill filtering and sorting, with visible count.
- Profile panel now has a section selector (overview/achievements/skills/chart/activity) to reduce clutter.
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
