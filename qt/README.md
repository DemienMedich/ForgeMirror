# Qt Widgets migration

## Branches

- `codex/pre-qt-2026-08-28`: exact stable ImGui snapshot, commit `7306152`, version 0.5.54.
- `codex/qt-gui`: incremental migration. `develop` and the preserved `codex/pre-qt-2026-08-28` baseline remain unchanged. This branch retains the ImGui compatibility client; its storage-conflict confirmation now uses the same locked core operation as Qt.

This is **stage 229**, not a feature-complete replacement for ImGui. Estimated functional migration remains **about 90%**, based on the breadth of user-facing scenarios in the coverage map below; this is an expert estimate, not a measured code or test percentage. The source audit confirms Qt surfaces for all 18 legacy workspace tabs, but action-level parity checks continue; stages 225–227 align legacy F6 **Логи** and F5 **Статистика профилей** with their Qt pages instead of the adjacent new Audit and Reports pages, and restore the profile dashboard's category-average and rank-share bars; stage 228 restores profile-manager search by login/profession, profession and archive filters, name sorting, and manual refresh; stage 229 restores the legacy Pomodoro sound-list refresh and discovery of installed `data/music` assets when the workspace has no local sounds. The remaining migration work is the broader action-level audit. After user-visible feature parity is closed, follow-up quality work includes hands-on NVDA/JAWS interaction testing, core events outside instrumented Qt workflows, and writes by older clients or tools that ignore the shared workspace lock. Current shared persistence paths use one reentrant cross-process lock; journaled Qt mutations hold it from recovery-snapshot creation through commit or rollback. Bulk task edits now use the same journal for task and audit files. Local Qt application-log updates re-read and merge under the lock, preventing one open window from erasing entries written by another; clearing the log requires explicit confirmation. Manual cloud pull/push and recovery, cloud file/catalog conflict transfers, storage-conflict resolution in both current clients, release downloads, and shared core cloud-sync mutators also coordinate through it. Profile ID allocation refreshes its cached sequence under the same lock before creating a profile. Pipeline edits, deletes, additions, and reordering now hold the shared lock while comparing their snapshot with the latest normalized pipeline; the full-list editor checks both its opening snapshot and live state, refreshes on conflict, and requires reopening before another save. External writers that bypass the lock remain outside that guarantee. Initial stable-workspace import verifies a staged copy against source content before activation. Both interrupted startup recovery and immediate checked rollback preserve changed in-flight files before restoring pre-images. Existing storage formats and domain services are reused. Qt's `AppTaskCompletionService` adds transactional cross-file recovery. Version `0.6.74` is the current Qt per-user installer; stages 47–229 are implementation checkpoints, not standalone releases. The preserved ImGui baseline remains version 0.5.54.

Banner and vault settings now use the shared core mutation service and workspace lock. Stale banner edits/deletes refresh the list and refuse the stale action; additions merge the latest saved list. Malformed banner JSON and symbolic-link targets are rejected without replacement. Vault settings reload the latest balance and journal before updating configuration, and mutation events omit phrase contents, amounts and currency values.

## Build and run

Design direction for subsequent UI work: [user-supplied interface references](../docs/design/INTERFACE_REFERENCES.md).
These guide composition and hierarchy; the existing dark/purple palette is unchanged.

```powershell
.\build-qt.ps1 -Package -PackageDirectory package-qt-0.6.74-release
.\package-qt-0.6.74-release\ForgeMirrorQt.exe
.\installer\build-qt-installer.ps1 -PackageDirectory .\package-qt-0.6.74-release
```

The portable directory is a QA output, not the release deliverable. The current installer is `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.74.exe`. Version `0.6.74` comes only from the root `VERSION` file and is propagated into the application, Windows EXE metadata, installer metadata and artifact name. Full install/update/uninstall evidence and SHA-256 are recorded in `docs/releases/ForgeMirror-0.6.74.md`.

Requires MSVC 2022, CMake and Qt 6.8+ Widgets/Test. Override the default installed Qt path using `-QtRoot`. The alternate package directory keeps an already-running package executable intact; use the default package-qt path when it is not in use.

On first launch, Qt offers to **copy** the stable workspace to its own local application data directory (`Pharos/ForgeMirrorQt/workspace`). Cancel aborts startup; No starts an empty workspace. Existing Qt workspaces are never reimported automatically. The staged import compares SHA-256 snapshots of source and copy before publishing, so concurrent source edits abort the import. Reparse/symlink entries are skipped. The stable client should remain closed while importing; failed attempts remove only their own staging directory.

Cloud operations are always scoped to the configured cloud folder and the isolated Qt workspace: manual pull creates a local backup before importing, and push previews the complete snapshot and requires a separate confirmation before publishing. Optional automatic synchronization is controlled in Cloud settings. The initial stable-workspace import remains a one-time copy; Qt never writes changes back into the stable ImGui workspace. `FORGEMIRROR_STORAGE_DIR` identifies the import source, not Qt's output directory. Failed first-run imports remove their own `import-<uuid>` staging directory.

`--storage-dir <path>` opens an explicit disposable development workspace. Production directory paths, parents and children are rejected. Do not deliberately point it at other live storage folders. A lock prevents multiple Qt clients from editing the same Qt workspace.

Admin mutations require the existing admin password from the copied settings (the existing core's initial default for empty storage is `admin123`). The admin login's **Не выходить после перезапуска** option persists the authenticated state only in this local Qt workspace; logging out clears it. Password changes require the current admin password and matching non-empty new password. When `FORGEMIRROR_ADMIN_PASSWORD` is set, persistent login and in-app password changes are disabled. Profile viewing remains available without a personal login session. Profile management is administrator-only; normal password changes require the current profile password. Personal unlock may remain session-only or trust this local Qt workspace for 30/90 days.

## Coverage

A source audit of the 18 legacy workspace tabs in gui/GuiWorkspacePanel.inc found a Qt surface for every one: Profile maps to the Profile page; Profiles to the Profile Manager dialog; Tasks, Projects, Skills, Professions, Pipeline, Pomodoro, Shortcuts, 3D viewer, Statistics, Logs, Rules, Banner, Storage and 3D settings map to corresponding Qt pages; Settings maps to Display Settings; About maps to the Program Information dialog. Qt adds Audit, Cloud and Administrator Profile Statistics pages. The same FORGEMIRROR_DISABLE_MODULES switches gate the corresponding modules in both clients.

| Area | Qt coverage through stage 229 | Remaining |
| --- | --- | --- |
| Profiles | Selector, compact level/XP/task metrics, persisted Overview/Analytics/Focus/Tasks modes (top-three skill preview, full skill list, key metrics, or ranked assigned-task dashboard); task dashboard counts active, overdue and completed-awaiting-XP work, emphasizes overdue rows and opens profile-scoped all/active/overdue/XP-pending lists, analytics category best-score bars, top-six total-XP skill bars and configurable 3–16-axis fractional-level radar, skill sorting by name/level/total XP/weight, legacy weight categories and inclusive range filters with persisted/resettable controls and total-XP column, transactional direct skill/global XP grant paired with profile audit, transactional admin edit of name/profession/spirit/block state paired with profile audit, archive/restore and password reset/change paired with audit in recovery journals, guarded permanent deletion of empty archived profiles with privacy-safe committed/failed/recovery-pending core events, rename with stable ID, session or 30/90-day local trust with transactional login/logout, expiry and stale-access revocation audits (including expiry/revocation while a process is active), achievements with title/skill search, expired visibility toggle, active/expired counts and soon-expiring preview plus grant/edit/revoke and local icons; wallet balance and admin adjustment, Pomodoro reward, personal evil-spirit removal, all paired with wallet audit in a recovery journal; wallet history, profile-creation audit, read-only profile audit and task/XP history including awarded and pending tasks, with filtered task-history and event-history CSV exports; administrator profile statistics across active and archived profiles with KPI summary, rank/XP/achievement leaders, rank distribution, category averages, inactivity and recovery lists, filters, refresh and UTF-8 CSV including team-value metrics; administrator TXT/CSV profile reports with XP progress, rank, last activity, recovery, category scores and skill detail; profile-manager list with name/ID/login/profession search, all/active/archive and profession filters, ID/name sorting, explicit refresh and a profession column | Stale profile snapshots and contending cooperating writes are rejected; older/direct writers that ignore the shared workspace lock remain uncoordinated |
| Tasks | List, search, status, priority, project, pipeline-stage, assignee, task-age and quick deadline/assignment/XP/action-needed/pipeline-signal filters with persisted selections; pipeline-risk summary links open each matching subset; sorting by creation, deadline or priority; visible-task CSV/TXT export; filter reset; select/clear visible task rows; date and assignee columns; XP/attention badges with reasons including missing/unknown/branching/final pipeline signals; pipeline-risk counts follow visible rows and search; overdue and work-focus row emphasis; details and awarded XP; restart-safe admin creation/status/edit/completion; confirmed single and multi-select deletion; awarded-task rollback follows verified per-profile snapshot chains across the entire selection in one recovery journal, refuses stale/legacy snapshots without deleting anything; in-app reminders for upcoming deadlines, including optional notification while the window is hidden in the system tray; on next launch, a once-per-interval recap of active deadlines crossed since the last check; opt-in aggregated Windows reminders while closed; administrator multi-select status, priority, project, pipeline stage, deadline and assignee edits; shared workspace lock serializes cooperating saves and journaled mutations from snapshot through commit or rollback | Older clients or writers that ignore the shared lock remain uncoordinated |
| Projects | Admin list, creation, editing and confirmed deletion with task detachment; stable IDs and current names in linked tasks; persisted search by project name/description, overdue and pending-XP filters, four sort modes; one reset clears search and both flags while preserving sort; open the selected project's tasks with project filter and unrelated task filters cleared | — |
| Skills | Catalog viewing/search, persisted profession filter for all/unbound/known/orphaned bindings, admin creation/editing and guarded deletion of unused records with checked persistence; confirmed merge transfers XP and achievements across active and archived profiles, updates task bindings/audit, and uses a recovery journal; manual cloud transfer paired with professions, with validation, paired backups and rollback on partial write | — |
| Pipeline | Stages, details, admin creation/editing, checked deletion of unused stages, atomic up/down reordering; current names in task rows, guided next-step transitions and read-only branch map with missing-link visibility | — |
| Professions | Admin list, creation/editing and guarded deletion; assignment through profile manager and skill editor; deletion clears bindings from active and archived profiles under a recovery journal; confirmed merge redirects profile assignments and skill bindings, deduplicates bindings and uses the recovery journal; manual cloud transfer paired with skills, with paired snapshot restore | — |
| Reports | Admin project/employee/pipeline-stage/category/status/priority/deadline-state/creation-month metrics with resolved names, search, persisted all-time/rolling/year/custom creation-date periods and optional comparison with the equal preceding period; current-status distribution chart, monthly completion trend from all retained status audit events within the latest 12 months, aggregate-to-task detail with status counts, overdue/XP-pending totals, resolved project/stage/participants and scoped XP by selection plus **Подробности** or double-click, and atomic UTF-8 CSV export of the selected grouping and cohorts | — |
| Audit | Read-only task audit for all users; administrators also see profile-access, local Qt application-log, storage-vault events and privacy-safe task CRUD/bulk/status, task XP, wallet, project/catalog, manual cloud transaction/recovery, administrator authentication, storage-health/cleanup and release-download/launch outcomes merged by timestamp, with persisted source plus actor/object/field filters, visible/total counts, and atomic UTF-8 CSV export of visible events; non-admin view/export remains task-only | Other core events outside the instrumented Qt workflows |
| Application logs | UI status feedback is attributed to the active module; Qt/runtime messages are secret-redacted; warning/error classification, 16-bin activity chart, persisted search/source/level filters, level totals/presets, persistent compact/autoscroll options, screen-reader names/descriptions for filters, presets, options and confirmed destructive clear action, UTF-8 export whose search matches displayed row numbers and messages; atomically persisted as one bounded 200-entry JSON history, with cross-window writes merged under the workspace lock | Non-UI core telemetry |
| Rules | Administrator F4 summary, checked editor, confirmed transactional level recalculation for active and archived profiles while preserving total XP, up to 20 named local rule presets and a 100-entry change history | Presets and history are Qt-local and are not synchronized to legacy clients |
| Display | Local 90/100/110/125/150/175/200% text scale, 60–100% main-window opacity imported from legacy `alpha` and included in custom Qt layout presets, adjustable spacing and detailed window/frame/scrollbar/grab rounding plus window/frame padding and item spacing, compact-table density, built-in Minimalism/Presentation/Compact quick layout presets that keep the fixed palette, saved Qt layout presets with safe read-only import of legacy UI presets (legacy colors/backgrounds/profile trust are ignored), persistent fullscreen toggle with F11, F10 frameless-mode toggle with native drag handle, opt-in close-to-tray mode, all 18 navigation pages restored by their saved index; compact icon-and-label navigation with keyboard hints and a visible active marker; keyboard-first search, Escape clear, accessible names/descriptions for navigation/search/table, filters and core display/background/admin-login dialogs; per-page legacy PNG backgrounds with opacity and tiling; fixed migration palette | External NVDA/JAWS manual interaction audit |
| Other | Separate workspace, rotating banner with administrator phrase management, guarded manual cloud pull, administrator-only full cloud push with an isolated preview, explicit confirmation, a preimage snapshot, per-file atomic replacement, stale-target checks and startup rollback recovery; opt-in automatic pull/push at the configured 1–120 minute interval through the same recovery transactions, push-before-pull for administrators, unlocked viewer wallet upload, deferral while modal editors are open and telemetry/status feedback; global header quick-sync matching configured pull/push and unlocked-wallet directions plus a 2-second health/drift indicator; manifest client version and data-update timestamp, bounded download of safe-name EXE/MSI installers with atomic replacement, local-copy SHA-256 and separately confirmed launch; task/pipeline/project/banner/rules/profession/skill comparison, per-file cloud-to-local/local-to-cloud apply and local snapshot restore (profession and skill catalogs move and restore as a validated pair), explicit administrator storage conflict resolution, read-only extended storage-health report (sync-file validation, content drift and stray-element inventory), administrator-only checked cleanup of re-scanned stray paths with stale-inventory guard, explicit nested-directory approval, no recursive deletion or reparse traversal; refresh, contextual keyboard shortcuts, local program shortcuts with add/open/reorder/delete and a persistent header quick-launch menu with process status, F1–F6 navigation, shortcut help, F10 frame toggle and Ctrl+F10 settings reset preserving profile trust and application data; Pomodoro timer/settings/sounds with administrator refresh and packaged `data/music` fallback, global header status, start/pause, next-interval and reset actions, guarded rewards; administrator vault settings/log, OBJ/FBX wireframe viewer and persisted 3D controls; program information and canonical version in Qt client | — |

### Qt rule presets and history (stage 122)

The administrator Rules editor can save up to 20 named local presets, replace or remove a preset, and load one into the edit form. Loading never writes `gameplay.ini` or recalculates profiles; the administrator must still save rules and separately confirm profile recalculation. Presets are stored in `meta/qt-rules-presets.json`, outside the explicit cloud synchronization file set.

Successful rule changes append before/after snapshots and changed-field names to an atomic, bounded 100-entry `meta/qt-rules-history.json`. History is read-only in the editor and likewise remains local to the Qt workspace. Corrupt, oversized, or symlinked preset/history files are rejected rather than followed or overwritten. Tests cover preset persistence/reload, history display, and that loading a preset leaves live rules unchanged until an explicit save.

### Application events in audit (stage 123)

Administrators can select **Приложение** in the Audit source filter to inspect locally persisted Qt UI/runtime activity alongside task and profile audit events. Rows retain their timestamp and module source; severity appears as the field and the sanitized event text remains searchable/exportable. This view reads the existing bounded Qt log and does not widen the log file set uploaded by cloud sync. Non-administrators remain restricted to task audit entries.

### Core task XP outcomes in audit (stage 126)

The task-completion workflow reports only a generic success or failure outcome to the existing bounded local application log. At startup, recovery of an interrupted transaction is also recorded. The administrator Audit page exposes both under **Core-событие**, separate from general Qt activity and task field history. Messages contain no task/profile identifiers or free-form service errors; the existing 200-entry persistence cap and clear/export behavior remain authoritative. Non-administrators cannot select, view or export this source. This currently instruments the Qt task-XP and journaled wallet mutation workflows; other core-service events remain outside Qt telemetry.

### Windows deadline checks while ForgeMirror Qt is closed (stage 128)

A separate switch in Qt display settings explicitly opts in to a current-user Windows Scheduled Task that runs the installed executable every 15 minutes with `--deadline-agent`; it is off by default and is available only for the standard isolated Qt workspace. Notifications contain counts of overdue tasks and tasks due within 24 hours, never task titles. The agent reads only the primary tasks file through the no-repair `LoadTasksDataReadOnly` API, suppresses unchanged summaries using an atomic local signature file, and does not initialize `QtWorkspace`, acquire the GUI workspace lock, import stable data, or access cloud settings. Disabling the option unregisters only the task with the expected app executable and arguments. The installer uninstaller invokes the same ownership-checked cleanup command. The integration test verifies deadline selection/dedup signatures and that the background read does not restore or rewrite a recovery backup. Host task registration was not activated during development verification.

### Storage vault events in audit (stage 124)

The same administrator-only source filter now exposes entries from the existing storage-vault history, including timestamp, action, amount/currency, and note. It reads the current bounded vault log without modifying it or widening cloud sync; non-administrators still see task audit only. `smoke_qt` verifies source isolation, amount/note rendering, and persisted filter bounds.

### Full-window completion trend (stage 125)

Statistics now reads task status events from the entire saved audit file, filtered to the earliest day shown on the 12-month chart. The Audit page and ordinary workspace snapshot retain their 200-row view limit; the report no longer loses monthly counts just because those older rows fell outside that table window. The test places a completion event beyond 200 newer events and verifies both it and the current-month total reach the chart.

### Skill merge (stage 92, implementation checkpoint)

Editing a skill to an existing catalog name asks for explicit confirmation and defaults to Cancel. Confirmed merge adds source XP to the destination skill in every active and archived profile, redirects source achievements and task bindings, deduplicates task skill IDs, and writes the task audit. The catalog, profiles, archives, achievements, task list and audit are covered by one recovery journal; the catalog is replaced atomically only after those writes are verified. The Qt smoke test exercises the UI confirmation, both profile states, merged XP, achievements, task audit and interrupted-transaction rollback. This is not an installer release; version `0.6.11` remains the latest lifecycle-verified installer.

### Profile task and XP history (stage 93, implementation checkpoint)

The protected profile history dialog now separates local profile events from saved task participation. Task rows show the task date, project, current status, contribution share, and global/skill XP; assigned tasks awaiting XP remain visible. Search filters by task or project, and the summary totals match the filtered rows. The history is derived from task records currently retained in the workspace, so permanently removed tasks and older task-log entries are not reconstructed. Qt smoke covers awarded and pending rows, totals, filtering and the existing profile-event tab. No installer lifecycle verification is implied.

### 3D viewer and settings (stage 47, implementation checkpoint)

When the existing `view3d` module toggle is enabled, the sidebar exposes a model viewport and an administrator-only settings page. OBJ and FBX meshes are rendered as wireframes; drag rotates, the wheel changes zoom, and the legacy automatic rotation control remains available. Models in the local `models/` directory are listed, with a file picker for another path. Viewer settings are stored in `[qt3d]` in the isolated workspace's `meta/ui.ini`; legacy `[view3d]` values are read as defaults, and unrelated INI sections are preserved on save. Existing Qt palette and navigation indexes are unchanged.

The application target compiled successfully for stage 47. No tests, installer packaging, or installer lifecycle verification were run, so version `0.6.11` remains the latest verified release. FBX parsing uses the already bundled ufbx source.

### Window display settings and context (stages 48–50, implementation checkpoints)

The Qt display dialog persists fullscreen and decorated/frameless mode alongside text scale and compact table density. F11 toggles fullscreen and saves the preference immediately, so the next launch restores it. Legacy `ui.windowDecorated`, `[profile] lastProfileId` and `[projects]` sort/filter options are imported as defaults. The Qt client saves the last selected profile, navigation page, task status, report view and project filters in `[qt]`; it does not import legacy unlock or trust values. Frameless mode exposes a compact drag handle using the platform's native move operation. The palette stays fixed as requested. These remain implementation checkpoints; no tests or installer lifecycle verification were run, and version `0.6.11` remains the latest verified release.

Stage 107 corrects the persisted page-index upper bound to 16 so the final navigation row (Logs) survives a restart. Values above the current final row clamp to Logs, and negative or malformed values still fall back safely. `smoke_qt` checks save/load plus restoration of the actual selected row.

### Project list filters (stage 51, implementation checkpoint)

The Qt project list now filters to projects with overdue tasks or pending XP and sorts by name, task count, overdue count or pending XP. Filter/sort state persists locally; old `[projects]` settings provide initial values. Task-status and report-view selections persist in the same Qt-specific section.

### Task priority filter (stage 52, implementation checkpoint)

The task list now filters by low, medium, high or critical priority alongside status. The selected priority persists in `[qt]` and uses the shared domain normalization, so unexpected stored values do not create a fifth category.

### Task project and pipeline filters (stage 53, implementation checkpoint)

The task toolbar can narrow by project or pipeline step, including explicit unassigned choices. Selections persist as project/step IDs, so renaming an item keeps the filter attached to it. Legacy tasks without IDs still match by their stored names. These filters apply only to the task list and do not alter task records.

### Task quick filters (stage 54, implementation checkpoint)

Quick filters now cover tasks assigned to the selected profile, due today, overdue, due within seven local calendar days, without a project, awaiting XP, or active. Their selection persists in `[qt]`. XP-pending means completed with no positive XP recorded for any participant, matching the existing client rule. The seven-day range uses local calendar boundaries.

### Administrator profile wallet adjustment (stage 55, implementation checkpoint)

The Qt Profile page exposes **Изменить кошелёк** to administrators. The dialog supports credit or debit with a positive amount, requires an audit reason, previews the resulting balance and asks for a separate confirmation. A debit larger than the loaded balance is disabled and rejected again on submission. Stage 94 wraps the existing `AppAdjustProfileWallet` mutation and `wallet_adjustment` audit append in one recovery transaction; an audit failure restores the profile bytes and leaves the modal open with the rollback result. The action is blocked while another recovery is pending. It does not change the central vault or push any wallet data to cloud. Stage 55 remains the original UI implementation checkpoint; no installer lifecycle verification is implied.

The Qt application target compiled successfully. The refreshed portable package is `Z:\CPP\ForgeMirror\package-qt\ForgeMirrorQt.exe`; startup remained alive in a disposable workspace with Qt removed from `PATH`, and `Qt6Gui.dll` plus `platforms/qwindows.dll` were present. No automated tests or installer lifecycle verification were run for this stage; `0.6.11` remains the latest verified installer.

### Statistics creation-date periods (stage 56, implementation checkpoint)

Statistics can show all tasks, the last 30 or 90 local calendar days, the current year, or a custom inclusive date range. The period is based on task `createdAt`; task status, overdue state and awarded XP are current values, not historical snapshots. In a bounded period, legacy tasks with no creation timestamp are excluded and their count is shown in the summary. Project and employee tables plus CSV export use the same filtered tasks. The range and custom dates persist in the Qt `[qt]` section of `meta/ui.ini` without changing other settings.

The Qt application target compiled successfully at the stage 56 checkpoint. No automated tests or installer lifecycle verification were run for that checkpoint; `0.6.11` remains the latest verified installer.

### Statistics task drill-down (stage 57, implementation checkpoint)

Selecting a project or employee row in Statistics and opening **Подробности** lists the matching tasks in the active creation-date period, with current status, creation/deadline dates and recorded XP. Employee membership uses task assignees and XP participants; project membership uses stable project IDs with the legacy-name fallback, including an explicit no-project group. This is read-only and uses the same filtered task set as the aggregate and CSV export.

The Qt application target compiled successfully at the stage 57 checkpoint. No automated tests or installer lifecycle verification were run for that checkpoint; `0.6.11` remains the latest verified installer.

### Statistics status distribution (stage 58, implementation checkpoint)

Statistics now includes a compact horizontal bar chart for new, in-progress and completed tasks in the selected creation-date period. It uses the same report cohort as the table and export, scales bars to the largest category, and shows exact counts. The caption states that statuses are current; reconstructing a historical trend from audit events is deferred. The widget uses the existing Qt palette and has an accessible name and text description.

The Qt application target compiled and its packaged startup was checked without Qt in `PATH`. Automated tests and installer lifecycle verification were not run; `0.6.11` remains the latest verified installer.

### Statistics drill-down shortcut (stage 59, implementation checkpoint)

Double-clicking a project or employee row on Statistics opens **Подробности** and shows the tasks belonging to that aggregate in the selected creation-date period. Single selection continues to refresh details when that pane is already open. Double-click behavior is limited to Statistics so task and catalog tables retain their own interaction patterns.

The Qt application target compiled successfully for stage 59. The refreshed portable package `Z:\CPP\ForgeMirror\package-qt\ForgeMirrorQt.exe` stayed alive for four seconds in a disposable workspace with Qt removed from `PATH`; `Qt6Gui.dll` and `platforms/qwindows.dll` were present. This is a startup smoke check, not an interaction test. Automated tests and installer lifecycle verification were not run; `0.6.11` remains the latest verified installer.

### Chronological audit view (stage 60, implementation checkpoint)

The administrator Audit page now merges task and profile events and orders them by timestamp, newest first. Its summary shows the total number of loaded events and the number currently visible after the shared search filter. The existing bounded profile log and task audit loader behavior are unchanged.

The Qt app and smoke target compiled, and packaged startup stayed alive for four seconds in a disposable workspace with Qt removed from `PATH`. The first full smoke run exposed a stale shortcut-help row-count expectation and a project-list fixture that was not refreshed after directly linking a task. Both test setup issues were corrected in stage 61; the suite now reaches and passes the audit-order assertion. No installer lifecycle verification was run; `0.6.11` remains the latest verified installer.

### Qt smoke fixture refresh (stage 61, implementation checkpoint)

The project-edit smoke scenario now re-renders the project table after it changes task/project links through the core service. The report-backed project list intentionally omits catalog entries with no tasks; the fixture now reflects that contract before attempting to edit a project. It also selects a current table cell explicitly so the same row drives the detail and edit actions.

`ctest --test-dir build-qt -C Release --output-on-failure` passes `smoke_qt` (1/1), including the reverse-chronological audit assertion. The packaged app startup check from stage 60 remains valid; installer lifecycle verification was not run, so `0.6.11` remains the latest verified installer.

### Audit CSV export (stage 62, implementation checkpoint)

The administrator Audit page can export the rows currently visible after the shared search filter. CSV values are quoted and escaped, the file uses UTF-8 with BOM, and `QSaveFile` commits without direct-write fallback. The default filename is timestamped and `.csv` is added when needed. Empty results do not open a dialog. The export does not include hidden events or change either audit log.

The smoke suite covers commas, quotes and embedded newlines, rejects malformed row widths and directory targets without replacing an existing file, and drives the actual Audit export dialog to verify the UTF-8 BOM, headers and exported visible-row count. `ctest --test-dir build-qt -C Release --output-on-failure` passes 1/1. The refreshed package `Z:\CPP\ForgeMirror\package-qt\ForgeMirrorQt.exe` stayed alive in a disposable workspace with Qt removed from `PATH`; installer lifecycle verification was not run, so `0.6.11` remains the latest verified installer.

### Audit source filter (stage 63, implementation checkpoint)

The Audit toolbar can show all events, task events or profile events. The selection is stored in the Qt `[qt]` section of `meta/ui.ini`; unrelated legacy settings remain intact. Search applies on top of the source filter, and the CSV action exports exactly the currently visible rows. The summary distinguishes all loaded events, events after source filtering, and rows visible after search.

The smoke test verifies source separation, persistence across settings reload, and that the export action uses the selected source. `ctest --test-dir build-qt -C Release --output-on-failure` passes 1/1; the refreshed package also stayed alive in an isolated workspace with Qt removed from `PATH`. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Focused audit search (stage 64, implementation checkpoint)

The Audit page adds independent case-insensitive filters for actor, task/profile ID plus old/new values, and field/action. They combine with source and general search filters; **Сбросить** clears all audit-specific and general search criteria and returns the source selector to all events. These transient fields are not persisted, matching the source ImGui log filters. The CSV exporter continues to serialize only rows remaining in the table.

`smoke_qt` verifies each criterion, intersecting criteria, reset behavior and interaction with the source selector; `ctest --test-dir build-qt -C Release --output-on-failure` passes 1/1. The refreshed package stayed alive in a disposable workspace with Qt removed from `PATH`. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Qt application session log (stage 65, implementation checkpoint)

The new **Логи** page captures Qt status-bar notifications and warning dialogs with timestamp and source, displays newest first, and filters by the shared search field and Info/Warning/Error toggles. Stage 74 later added the bounded local persistence described below; application log files are not included in the explicit cloud-transfer lists.

The Qt smoke test checks the page controls, search/level intersection, UTF-8 export and clear acknowledgement. `build-qt.ps1 -Package` passes `smoke_qt` (1/1) and `smoke_core`; the refreshed package stayed alive for four seconds in an isolated workspace with Qt removed from `PATH`. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Profile wallet activity history (stage 66, implementation checkpoint)

The Profile page exposes **История кошелька** only to an administrator or after personal profile unlock. The read-only dialog shows newest wallet events first: administrator credits/debits with their reason, Pomodoro rewards, and evil-spirit removal. Successful Qt Pomodoro rewards and spirit removals append profile-audit events in the same recovery transaction as their mutations; spirit removal also journals `meta/storage.json`. Existing wallet/admin and spirit event encodings remain compatible with the shared audit format. The history is a view over the existing capped profile audit file.

`smoke_qt` verifies access gating, all three operation types and UTF-8 reasons in the history dialog. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Project-to-task focus (stage 67, implementation checkpoint)

The Projects page adds **Задачи проекта** for the selected row. It opens Tasks with that project selected and clears search, status, priority, quick-task and pipeline filters so the destination actually shows the project's tasks. The selected project is highlighted by the existing table selection; no second navigation or separate focus state is introduced.

`smoke_qt` drives the action from a project linked to a real task and verifies the destination page, project filter and visible linked task. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Monthly completion trend (stage 68, implementation checkpoint)

The Statistics chart adds a 12-month line for task transitions into **Выполнена**, binned by the audit event timestamp. It is distinct from the current-status distribution above it. The application retains at most 200 task-audit events in memory, so the chart explicitly labels its source and limit; months with no retained completion event show zero and must not be read as proof that no completion occurred. This trend is independent of the task-creation cohort selected for the table and CSV. `smoke_qt` checks month boundaries, status filtering, oldest/newest month placement and exclusion of events outside the 12-month window. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### In-app task deadline reminders (stage 69, implementation checkpoint)

While Qt is open, it checks active tasks once per minute and once shortly after startup. The nearest unfinished task due within the next 24 hours produces a 10-second status-bar reminder with its title and deadline. Each task is reminded at most once per application run; overdue and completed tasks are ignored. These notifications are not delivered while the application is closed. `smoke_qt` exercises the reminder path deterministically and verifies the once-per-run guard plus exclusion of overdue/completed tasks. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Recap of deadlines crossed while Qt was closed (stage 105, implementation checkpoint)

Qt stores a small `lastCheckAt` watermark in `meta/qt-reminder-state.json`. At startup and on the existing minute timer, it reports active tasks whose deadlines fell after the previous check and by the current time. The recap lists at most three titles, logs the complete count locally, and advances the watermark atomically so timer races do not repeat an already delivered recap. A first launch without prior state starts its window at the current time and does not surface old overdue tasks. Completed tasks are skipped. No startup entry, scheduled task or background process is installed; if the user never launches Qt again, the application cannot notify them. `smoke_qt` verifies restart-gap recap, watermark persistence and duplicate suppression. Version `0.6.11` remains the latest installer with full lifecycle verification.

### Task action-needed quick filter (stage 108, implementation checkpoint)

The Qt Tasks quick-filter menu now includes **Требуют внимания**, matching the legacy filter. It includes tasks awaiting XP, active overdue tasks, tasks with a missing or unknown pipeline stage, and active tasks at a final stage whose handoff is not closed. When no pipeline is configured, pipeline checks are omitted. The filter is stored as value 8 in `[qt] taskQuickFilter` and survives restart. Tests cover each included reason, an unaffected task and persistence. The stable ImGui implementation is unchanged.

### Task age filter and sorting (stage 109, implementation checkpoint)

The Qt Tasks page now filters by creation age (`all`, 7, 30, 90 or 365 days) and sorts by newest, nearest deadline or highest priority, matching the legacy choices. Missing creation timestamps are excluded from bounded periods. Ties use creation time and then stable task ID. Both choices persist in the Qt `[qt]` section as `taskCreatedRange` and `taskSortMode`. `smoke_qt` checks the 7-day boundary, missing timestamps, priority ordering, persistence, and clamping of invalid settings. The stable ImGui implementation is unchanged.

### Task assignee profile filter (stage 110, implementation checkpoint)

The Qt Tasks page can now filter by any profile, matching the legacy profile filter. A task matches when the selected profile appears among its assignees or XP participants. The stable profile ID is stored as `taskAssigneeProfileId`; if that profile no longer exists, the filter returns to **Все профили**. `smoke_qt` covers assignee and participant matching, exclusion of unrelated tasks, and persisted selection. The stable ImGui implementation is unchanged.

### Visible task export (stage 111, implementation checkpoint)

The Tasks page now exports visible rows to UTF-8 CSV or TXT. Export reads the rendered table, so status, priority, profile, project, pipeline, age, quick filters and search all apply; hidden rows are not included. CSV quotes commas, quotes and multiline values and includes task, deadline, project, stage, XP participant and scoring fields. Both formats have a UTF-8 BOM and use `QSaveFile` for atomic replacement. `smoke_qt` drives both real save dialogs, checks escaping and confirms search-hidden rows are excluded. The stable ImGui implementation is unchanged.

### Reset task filters (stage 112, implementation checkpoint)

The Qt Tasks toolbar now has **Сбросить фильтры**, matching the legacy reset action. It clears the shared search field and resets status, priority, quick, creation-age, assignee, project and pipeline filters plus sort order. The selection is saved once and the table refreshes once. `smoke_qt` checks the visible result and persisted defaults. The stable ImGui implementation is unchanged.

### Visible task selection tools (stage 113, implementation checkpoint)

Administrators have **Выбор задач → Выбрать все видимые / Снять выбор** on the Tasks page, matching the legacy bulk-selection controls. “Visible” means rows remaining after all filters and search; hidden rows are not selected. Selection feeds the bulk-edit dialog and atomic **Удалить выбранные** action, both enabled only with at least two valid selected tasks. Multi-delete writes one recovery journal spanning task/audit files and every affected profile. Each profile's selected rollback snapshots are applied in reverse postcondition-chain order; if current progress is stale, missing, ambiguous, or incompatible, the whole selection is refused unchanged. `smoke_qt` verifies filtered selection, hidden-row exclusion, UI confirmation, multiple task deletion, exact file restoration after injected audit failure, valid multi-award rollback and stale-progress refusal.

### Task table creation date and assignees (stage 114, implementation checkpoint)

The Qt Tasks table now shows task creation time and a compact assignee summary (up to two names plus a remaining count), falling back to XP participants when no assignee is explicitly set. These values participate in the existing search, so a profile name can find its tasks without opening details. `smoke_qt` checks column layout, timestamp, resolved profile name and assignee search. The stable ImGui implementation is unchanged.

### Task attention badges (stage 115, implementation checkpoint)

The Qt Tasks table now marks tasks awaiting XP with **XP** and tasks needing action with **!**. Hovering the task title explains the reason: pending XP, overdue deadline, missing/unknown pipeline stage or an open final-stage handoff. The same computed conditions drive the existing **Требуют внимания** quick filter. `smoke_qt` checks each reason, a normal task and a task whose XP is already awarded. The stable ImGui implementation is unchanged.

### Bulk task status change (stage 70, implementation checkpoint)

Stage 70 introduced administrator multi-select status changes between **Новая** and **В работе**; completed tasks remained outside that XP-sensitive workflow. The migration information dialog also stopped claiming that the already-ported 3D viewer was missing. Stage 71 consolidates these controls into the dialog described below.

### Bulk task status and priority (stage 71, implementation checkpoint)

On Tasks, administrators can select multiple rows and use **Массовое изменение** to set status or priority. Completed tasks are excluded from the status option, while priority changes remain available. Status completion and XP distribution still use the individual task workflow. Existing core bulk mutation services persist each change and append task-audit events. `smoke_qt` covers admin gating, both operations, persisted task values and audit events, plus priority-only behavior when a completed task is selected. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Bulk task assignee edits (stage 72, implementation checkpoint)

The same administrator dialog can assign one or more active profiles to all selected tasks. Archived profiles cannot be selected. Tasks with XP participants are skipped to preserve the existing completion and reward workflow; the dialog previews how many selected tasks will be skipped, and the result message reports changed and skipped counts. The existing core service persists each eligible assignment and appends task-audit events. `smoke_qt` covers active/archived profile selection, empty-selection gating, persisted assignments, XP-task skip behavior, audit scope and continued priority-only editing for a selection containing a completed task. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Bulk task project, pipeline and deadline edits (stage 73, implementation checkpoint)

The administrator multi-select dialog now also assigns or clears a project, assigns or clears a pipeline stage when that module is enabled, and sets or removes a deadline. Existing domain services persist the edits and add task-audit events; the selection containing completed tasks still excludes only bulk status changes. `smoke_qt` verifies the selected project and stage IDs/names, the persisted deadline, and the dialog's available operations. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Persistent local application log (stage 74, implementation checkpoint)

The Qt application log now restores its latest 200 entries from `meta/qt-application-log.json` in the isolated Qt workspace. Each update uses `QSaveFile`; symlink targets are rejected, and the page reports when persistence fails. Clearing the log writes an empty history before the usual clear acknowledgement is recorded. The file is not included in the explicit cloud-transfer lists. `smoke_qt` verifies export and clearing plus recovery of prior entries when a second window opens on the same workspace. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Qt runtime warnings in the application log (stage 106, implementation checkpoint)

After the main window is created, Qt warning and critical messages are forwarded to the local application log as `QtRuntime` entries; debug and informational framework chatter is ignored. Delivery is queued to the GUI thread. Context file paths and line numbers are not stored, messages are capped at 2048 characters, and common password/token/secret/authorization fields, bearer values and URL user-info are redacted. The previous Qt message handler continues receiving each event. Fatal Qt errors retain Qt's normal termination behavior and are not captured. Packaged `--smoke-test` emits synthetic warning and critical probes, including dummy credentials for testing redaction without touching production data. Early startup failures before the window exists remain outside this log.

### Optional tray reminders (stage 75, implementation checkpoint)

Display settings offer **При закрытии сворачивать в трей и продолжать напоминания**, disabled by default and available only when the platform reports both a system tray and notification support. With it enabled, closing the main window hides it while the Qt process and existing one-minute deadline timer remain active. Upcoming-task reminders appear as native tray notifications; clicking the icon or choosing **Показать ForgeMirror** restores the window. **Выход** in the tray menu terminates the process, and reminders stop. This is not an OS scheduled task or login auto-start, so it does not send notifications after a full process exit. No background task is installed or removed. `smoke_qt` verifies setting persistence and, when the test host exposes a tray, the hide/notify/restore flow. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Skill catalog profession filter (stage 76, implementation checkpoint)

The catalog filter switches between all skills, skills without a profession, a known profession, or a retained skill binding whose profession record no longer exists. Orphaned IDs are shown as `Неизвестная: <ID>` instead of disappearing from filter choices. The selected ID is stored in the existing local `meta/ui.ini`; it does not rewrite the skill catalog or profile data. Tests cover all four selection modes and reopening the window with the orphan selection restored. The constructor now restores the saved navigation row before any context-save signal can overwrite persisted display choices. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Pipeline branch map (stage 77, implementation checkpoint)

The Pipeline page opens a read-only map grouped by the existing branch labels. Each stage shows its code, title, responsible person and outgoing count; arrows follow the saved `nextIds` and node details list the selected stage's description and destinations. References to deleted or unavailable stages appear as separate warning nodes rather than being dropped. Dragging pans the canvas and Ctrl+wheel zooms it. The map performs no writes; stage edits remain in the existing editor. `smoke_qt` checks branch transitions, an unavailable target and selected-node details. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Profile activity history (stage 78, implementation checkpoint)

The Profile page adds a read-only **История профиля** dialog for an administrator or the unlocked selected profile. It shows the newest matching events in the existing local `meta/profile-audit.log`, with Russian labels for known actions and the original action name for future/unknown entries. The source remains bounded to the latest 500 workspace audit lines. Task and XP history have a separate tab. Wallet mutations pair their audit append with the balance update in a recovery transaction; other profile/security audit events remain best-effort. Tests cover visibility before/after personal unlock, task and XP rows, wallet rollback and unknown-action fallback. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Transactional wallet and audit writes (stage 94, implementation checkpoint)

Administrator wallet adjustments, Pomodoro rewards and evil-spirit removal now share a `FORGEMIRROR_QT_PROFILE_WALLET_1` journal. It snapshots the selected profile and profile audit; spirit removal also snapshots `meta/storage.json`. The balance/vault mutation and audit append commit together, and failures restore the exact snapshot before returning an error. Interrupted journals use the existing startup/refresh recovery path. Qt smoke injects an audit failure after the profile mutation for administrator adjustment and after both profile/vault mutation for spirit removal; both flows verify restored balances and no pending journal. No installer lifecycle verification is implied.

### Direct XP audit transaction (stage 95, implementation checkpoint)

Manual administrator XP grants now include `meta/profile-audit.log` in the same recovery journal as the profile and achievement data. The XP grant and its `direct_xp` audit event either commit together or roll back together; old two-file direct-XP journals remain readable. The Qt smoke test injects an audit-write failure and simulates interruption after the profile, achievement and audit files have changed, then verifies exact restoration and the committed audit details. Packaged startup and `--smoke-test` were also verified in a disposable workspace. This remains an implementation checkpoint; version `0.6.11` is still the latest installer with full lifecycle verification.

### Transactional profile editing audit (stage 96, implementation checkpoint)

The administrator profile editor now saves name, profession, spirit and blocked-state changes together with a `profile_edit` event listing the changed fields. A dedicated recovery journal snapshots the profile INI and `meta/profile-audit.log`; an audit failure or interrupted write restores both. The recovery allowlist accepts only the selected root profile and audit file, with tests for failed profile writes, injected audit failure, successful audit details and interrupted journal recovery. Archiving and password/security events remain outside this transaction. No installer lifecycle verification is implied; `0.6.11` remains the latest verified installer.

### Transactional profile lifecycle audit (stage 97, implementation checkpoint)

Password changes and administrator resets now append `password_change` or `password_reset` in the same recovery transaction as the profile INI; password values are never included in audit details. Archive and restore operations journal the root profile, archived profile and audit log together, so a failed audit rolls back the file move. Recovery validates exact paired IDs and only permits those paths. Smoke tests cover audit failures for password reset and archive, successful audit rows, recovery after interrupted archive movement and the unchanged-profile guard after failed storage writes. Unlock/trust/session events remain best-effort. No installer lifecycle verification is implied; `0.6.11` remains the latest verified installer.

### Transactional session access audit (stage 98, implementation checkpoint)

Session login and trusted login now require a successful `unlock` or `trusted_unlock` audit write before the in-memory session is granted. Trust creation and the matching login audit share a recovery journal that restores the local trust settings and audit log together on failure. Explicit trusted logout revokes trust even if its `lock` audit append fails, and always closes the in-memory session. Tests inject audit failures for session login, trust creation/restoration and logout, verify exact rollback where required, and retain coverage for normal login, expiry and sharing-locked settings. `smoke_qt` and `smoke_core` pass; the packaged 0.6.11 installer remains unchanged and no installer lifecycle verification is implied.

### Trust expiry and stale-session revocation audit (stages 99–100, implementation checkpoints)

Expired trusted entries now get removed only together with a `trust_expired` audit event in a profile-session recovery journal; an audit failure restores the local settings. A trusted entry whose profile is blocked, archived, or otherwise unavailable is revoked together with a `trust_revoked` event in one transaction. An already-open session whose profile fingerprint or availability changes is closed and its remembered trust removed; if that removal fails, a local session-close/failure event is attempted. Tests cover audit failure without partial expiry cleanup or partial revocation, successful retry, profile password changes, blocking, archive transitions, trusted restoration, and logout. These remain implementation checkpoints on 0.6.11, not an installer release.

### Profile trust audit labels (stage 101, implementation checkpoint)

The profile activity history now renders localized labels for trust expiry, successful trust revocation, and failed revocation instead of exposing internal event IDs. Its UI test verifies all three labels while retaining an unknown-event row as a raw value for forward compatibility. No data migration or audit-log format changed.

### Transactional whole-workspace cloud push (stages 102–103, implementation checkpoints)

The Cloud page simulates legacy push rules against a private cloud copy and reports added, replaced and removed files. After a separate cancel-default confirmation, the apply phase verifies that the reviewed plan is still current, refuses unresolved `storage.json` conflicts, snapshots each changed cloud file locally and records a recovery journal before writing. Replacements use `QSaveFile`; interrupted pushes roll back on the next workspace startup, but only when every affected file still matches either its preimage or the transaction's expected postimage. Concurrent unrelated cloud edits are left alone; ambiguous edits stop recovery without overwriting them. Absolute manifest paths are remapped only when inside the configured cloud root, and existing release metadata is retained in the staged plan. Tests cover exact backup contents, successful apply, failure after a partial apply, startup recovery and rejection of an outside manifest. Version 0.6.11 remains the latest lifecycle-verified installer.

### Application log level presets (stage 79, implementation checkpoint)

The Qt Logs page adds one-click presets for all levels, warnings plus errors, and errors only. They update the existing level checkboxes and reuse the same filtering path, so search, export of visible entries, stored log data and palette behavior remain unchanged. Tests verify each preset's checkbox state and page visibility. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Application log source filter (stage 80, implementation checkpoint)

The Qt Logs page lists distinct sources present in the local application log and can show one source or all sources. The filter intersects with level and text search, and TXT export uses the same visible source selection. Sources are refreshed from persisted entries; no log records or palette settings are changed. Tests verify source listing and that filtered rows contain only the selected source. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Application log display options (stage 81, implementation checkpoint)

The Qt Logs page now offers compact rows (hiding time and source columns) and autoscroll to the newest row at the top of the table. Both preferences persist in the Qt `[qt]` section of `meta/ui.ini` and restore when the window is reopened. The existing table density setting still applies independently. Tests cover saved values, live column visibility and restart restoration; installer lifecycle verification was not run, and `0.6.11` remains the latest verified installer.

### Read-only task audit access (stage 82, implementation checkpoint)

The Audit page is now available to every user when the task module is enabled, matching the legacy Logs panel's task-history visibility. Non-administrators are restricted to task events: profile activity is not loaded into their view, the source selector is hidden, and CSV export remains administrator-only. Existing actor/object/field/search filters continue to apply. Tests verify task rows are visible while profile rows and export controls are not. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Task audit export access (stage 83, implementation checkpoint)

The read-only task audit export is now available to all users, matching the legacy Logs panel. Non-administrator exports are built from the already-filtered task-only table and guarded again against profile-source rows; administrators retain export of whichever task/profile rows are visible. Tests cover the real non-admin save dialog and verify the resulting CSV contains task records but no profile events. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Application log level summary (stage 84, implementation checkpoint)

The Qt Logs summary now reports total info, warning and error entries alongside the filtered visible count. Level totals describe the complete locally retained log and do not change with source, text or level filters. Tests compare the displayed totals with the persisted log entries. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Filter-consistent log export (stage 86, implementation checkpoint)

The Qt application log export now searches the same timestamp, level, source and message fields as the visible table; row numbers are output labels and do not affect filtering. Dialog and button text describe export of the local journal with current filters, including entries restored from earlier launches. `smoke_qt` opens the actual save dialog and verifies UTF-8 BOM plus exact filtered content when the search term also appears in row numbers. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Application log activity chart (stage 85, implementation checkpoint)

The Qt Logs page now plots the retained messages across 16 time intervals, using the full local log regardless of active filters. Entries spread across their timestamp range; if timestamps are identical, the chart falls back to record order. The chart uses the existing Qt palette and exposes its bin counts accessibly. Tests cover timestamp endpoints, identical-time fallback, chart visibility and inclusion of all persisted entries. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Guarded manual cloud pull (stage 43)

The **Облако** page is available to every user and reads the existing `meta/cloud.ini` format. It reports whether configuration is enabled, the resolved root, folder availability, stable-client automation flags, manifest version and pre-sync file differences. **Получить из облака** is enabled only for an enabled, available external folder and defaults to cancellation in an explicit confirmation dialog.

Qt rejects overlapping roots, symlinks and Windows reparse points, then runs the existing pull against a disposable staging copy. A storage conflict or malformed changed JSON aborts before local writes. Before applying changed files, it keeps a complete sibling backup named `qt-cloud-backup-<uuid>` and atomically records a SHA-256 checked recovery journal at `meta/qt-cloud-pull.json`. Each destination is replaced through QSaveFile. A write failure restores only journaled files; startup and reload recover an interrupted commit before loading data. An invalid journal blocks loading. Backups stay outside the workspace so repeated pulls do not recursively copy earlier backups. Successful data is reloaded from disk. The confirmation explicitly warns that cloud files replace local edits; difference counts cover tasks/pipeline only, not every transferred file. A separate administrator-only full push is available with preview, confirmation, preimage backup and startup recovery. At the stage 43 checkpoint, automatic sync, wallet transfer and release download were unsupported; stages 156–157 now add scheduled sync and the update installer workflow. Full automatic conflict resolution remains unsupported.

The manual per-file conflict dialog covers tasks, pipeline, projects, banner phrases, gameplay rules and professions. Each local/cloud replacement requires an explicit confirmation; local and cloud preimages are saved in `meta/updates`, writes are atomic, and source/destination snapshots are rechecked before the cloud file is replaced. The shared backup browser recognizes project, banner, gameplay and profession snapshots. This targeted resolver remains a six-file operation alongside the transactional full push. Gameplay INI transfer rejects files without recognized settings or with malformed numeric values; profession catalog transfer rejects invalid UTF-8 and rows without both ID and name.


### Automatic cloud synchronization (stage 156)

Qt now runs the existing opt-in 1–120 minute schedule. It honors `autoPull` and administrator-only `autoPush`; administrators push first when both are selected. Viewer sessions may upload only the selected profile wallet while that profile is unlocked. Workspace pull and full push use Qt's staged/journaled recovery transactions. Pull changes reload the workspace and lock profile access; a recovery journal left after failure disables the window and exits for startup recovery. A modal editor or confirmation defers a due run to the next timer tick. Settings explain that scheduled pulls do not ask for per-run confirmation and can replace corresponding local sync files; pull backups remain available outside the workspace.

Tests cover interval boundaries and malformed interval clamping, disabled schedules, viewer push restrictions, a real viewer pull with a backup, and administrator full push. `--Package` smoke and installer lifecycle verification remain separate release gates.

### Cloud release installer (stage 157)

When the manifest version is newer, the Cloud page offers **Скачать установщик** and **Запустить установщик**. Only a single safe `.exe`/`.msi` filename is accepted; cloud and local update paths reject symlinks/reparse points, downloads are capped at 256 MiB and saved with QSaveFile. The downloaded bytes are compared with the bytes read from the source and their local SHA-256 is shown. The manifest format has no expected digest or signature, so this confirms copy integrity only; it does not authenticate the publisher or source. Launch always asks for confirmation. Tests cover successful/replaced downloads, invalid/traversal/device names, overlapping roots, visible action state, download click-through, and cancellation of launch. This stage does not change the canonical application version or installer artifact.

### Task and pipeline conflict recovery (stage 44)

**Сравнить версии** opens a compact two-tab dialog for `meta/tasks.json` and `meta/pipeline.json`. Each tab shows separate local/cloud rows with domain entry counts, byte size and an elided full-path cell. The five newest compatible snapshots from `meta/updates` are listed below with 40-pixel restore actions. Confirmation defaults to cancel and repeats the source, destination and effect before writing.

Applying cloud data or restoring a snapshot changes only the isolated local Qt workspace. The source must be a regular non-symlink JSON file and is re-read immediately before commit to reject stale previews. The current local file is first written as a compatible timestamped snapshot, cloud acceptance also stores the cloud source, and the target is replaced using `QSaveFile` without direct-write fallback. Malformed JSON and Windows sharing locks leave the target bytes unchanged. Stage 44 did not modify the cloud root, resolve `storage.json`, prune snapshots or perform a full domain-schema/cross-reference audit.

### Guarded task and pipeline push (stage 45)

The same comparison dialog can explicitly upload one local `tasks.json` or `pipeline.json`. It never sends the whole workspace. Before replacement, the current local source and existing cloud target are copied into compatible local `meta/updates` snapshots. Source and target are re-read after preview; an intervening change aborts the operation. The destination uses `QSaveFile` with direct-write fallback disabled, so a sharing lock or commit failure leaves the cloud bytes unchanged. The configured root must exist, remain separate from the workspace and contain no symlink/reparse traversal.

Stage 45 push is manual and limited to tasks/pipeline; stages 87–90 add projects, banner phrases, gameplay rules and professions to the same individually confirmed comparison and backup flow. Automatic pull/push, timers, background conflict decisions and snapshot pruning remain disabled.

### Storage conflict recovery (stage 46)

An administrator-only action appears when local and cloud `meta/storage.json` differ. The comparison shows balance, currency, revision, journal size, update time and paths. Resolution always selects one complete wallet version; balances and journal entries are never guessed, added or merged. Confirmation defaults to cancel and explicitly warns that the other balance, journal and reward rules will be replaced.

Both original files are first written to timestamped local `meta/updates/storage.local.*.json` and `storage.cloud.*.json` snapshots. Both JSON structure and the canonical vault `content_hash` are validated, source and target are re-read to reject stale previews, roots and reparse traversal are guarded, and replacement uses `QSaveFile` without direct-write fallback. Automatic resolution remains disabled.

### Rotating banner and phrase management (stage 41)

Qt now shows the first stored phrase in a compact strip below the profile toolbar and advances through the local phrase list every 60 seconds. Empty lists collapse the strip. The administrator-only **Баннер** page ports phrase listing, add, edit and confirmed deletion; every successful mutation refreshes both the strip and table immediately.

The editor serializes a complete draft with the existing banner format in a temporary directory, reloads it for validation and replaces `meta/banner.json` through `QSaveFile` with direct-write fallback disabled. Symbolic-link targets and parents are rejected. Tests cover Cyrillic editing, empty-result deletion, a real Windows sharing lock with byte-identical preservation, administrator visibility, live strip refresh and both native layouts without clipping.

### Local program shortcuts (stage 40)

The **Ярлыки** page ports the stable local launcher for every user. It lists the stored label, path and current availability, adds an existing file through a compact picker dialog, opens the selected path through `QDesktopServices`, reorders entries and removes only the shortcut record after confirmation. Deleting a shortcut never deletes its target file, and no path is interpolated into a shell command.

Shortcut JSON now uses the shared checked atomic replacement path on Windows. A failed write keeps both the in-memory order and the previous file bytes. Tests cover add/reload/reorder, injected persistence failure with rollback, the real non-administrator page and dialog, target preservation after deletion, and native layouts without clipped controls.

### Profile management

On the Profile page, an administrator can open **Управление профилями**. The searchable list includes an optional archive view. Creation uses the existing service and generates a login/password; credentials are masked until explicitly revealed. The login can be copied directly, while password copying is enabled only after revealing it. Administrator password reset generates a random password, shows the profile login and provides the same explicit reveal/copy flow before closing. Credentials are never written to logs.

Editing changes display name, profession, spirit and blocked state in a single profile save, preserving the stable profile ID, XP, wallet, credentials and history. Unknown existing profession IDs remain selectable rather than being silently erased. Empty or control-character names are rejected before writing. Archiving requires confirmation, reports assigned active tasks, and preserves the profile and task history; restore is reversible. Archived profiles cannot be edited or receive XP. Permanent deletion is limited to empty archived profiles.

### Stable profile rename (stage 35)

The profile editor can change the visible name while keeping the file name and profile ID unchanged. Task assignees, XP rollback participants, audit entries and stored credentials therefore retain their existing identity links. Name, profession, spirit and blocked state are persisted in one checked profile snapshot rather than through separate partial writes.

Both Qt and the domain snapshot service reject blank names and ASCII control characters; surrounding whitespace is normalized. Tests cover validation, an injected profile-write failure, successful Cyrillic rename, unchanged ID/XP/wallet/login/password/skills and refreshed manager display. Native QA captures the actual editor.

### Direct XP grant (stage 38)

The Profile page now exposes **Добавить XP** to administrators for the selected active profile. This ports the stable `addxp` operation rather than inventing a synthetic completed task: the entered base amount is added to global XP, while the selected skill receives the same amount multiplied by its currently active achievement bonus. Task category, score, spirit, repeat and recovery modifiers do not apply. The dialog previews both resulting amounts before saving.

The operation rejects missing catalog skills, blocked or unavailable profiles, non-positive values and integer overflow. Manifest `FORGEMIRROR_QT_DIRECT_XP_1` snapshots the profile INI and its achievement JSON before either can be rewritten. A checked save failure restores byte-identical files immediately, and an interrupted operation is recovered on startup or refresh. Tests cover injected failure, interrupted two-file recovery, a 50% achievement bonus, global/skill persistence, the blocked-profile guard and the actual Qt form.

### Storage vault administration (stage 39)

Administrators now have a **Хранилище** page that shows the encrypted central balance, currency code, recent operation log and the effective Pomodoro reward window. The page does not expose arbitrary balance editing. Its settings dialog ports the stable controls for currency name/code, retained log count, reward start/end, minimum focus duration, coins per cycle and enabled weekdays.

Saving copies the complete current vault, including balance and log, through the canonical serializer in a temporary directory and reloads it for validation. Qt then replaces `meta/storage.json` with `QSaveFile` and direct-write fallback disabled; symbolic links and pending recovery transactions are rejected. Tests verify Cyrillic settings, preservation of balance/log, all reward fields, a real Windows sharing lock with byte-identical failure, the administrator-only navigation page and the production dialog route.

### Transactional rules recalculation (stage 37)

The Rules page now offers an explicit administrator action that previews the number of active and archived profiles and defaults to Cancel. Recalculation preserves each profile's accumulated total XP, synchronizes its skill catalog, and derives level and progress from the currently saved curve. Archived profiles remain archived.

Every target ID and path is validated before mutation. Manifest `FORGEMIRROR_QT_RULES_REAPPLY_1` snapshots each active INI and, for archived profiles, both the archive location and the temporary active location used by `FileStorage`. Only then are all profiles loaded before profile content is rewritten. A failed save restores byte-identical files immediately; an interruption before commit is recovered on the next startup or refresh. The manifest accepts profile INIs only, rejects duplicate IDs and links, and cannot include task or configuration files. Tests cover partial-write rollback, interrupted recovery, successful active/archive commit, total-XP preservation and the real confirmation action.

### Empty archived profile deletion (stage 34)

The profile manager exposes **Удалить навсегда** only for an archived selection. Before deletion the service rejects every task assignment or XP participant reference and opens the archived profile under recovery protection to check administrator status, global and skill progress, category state, task counters, wallet, queue and achievements. Any meaningful state keeps the profile in the archive.

An allowed deletion journals the active and archived INI locations plus the profile achievement file. This covers the temporary archive-to-active move used for validation as well as final file removal; an interrupted process restores the original archived bytes and removes any transient active copy on the next Qt startup. Unsafe IDs, links, mismatched journal paths and incomplete manifests fail closed. Tests cover task/progress guards, interrupted recovery, committed removal and the real profile-manager confirmation path.

Administrators can reset a selected active profile's password. The overflow menu also offers a normal password change, which checks the current password and confirmation. Blocked profiles and profiles without a password require administrator recovery. Pending XP recovery blocks profile mutations as well.

The profile overview now uses four compact 56px metric blocks, following the recorded design references without changing the palette. The stable ImGui frontend and its data remain untouched.
### Task completion and recovery

As administrator, select a task, choose **Изменить статус → Выполнена — начислить XP**. The dialog supports category, score 1–10, participant contributions totaling 100%, and skill ratings 0–5. Ratings automatically produce skill percentages; the preview shows each participant's global and skill XP. The task's stored deadline penalty is applied unconditionally, matching ImGui. Repeat/recovery penalties, achievement skill bonuses, spirits, category best scores, cooldowns and task counters follow the existing XP calculation. Participants and skills remain editable in this dialog; task assignments provide initial selections.

Completed legacy tasks without participants can receive their pending XP. Already awarded tasks cannot receive XP again, including after reopening. Reclosing such a task changes only its status. A 100% task penalty permits completion with zero XP and still records participants/counters; unlike the old ImGui loop, zero-pool participants are not silently skipped. Blocked/archived/missing profiles are rejected. Excessive XP values are rejected before unsafe integer arithmetic.

Before writes, `meta/qt-xp-transaction` receives original profile bytes, tasks, task audit and the last-good task backup. Failed writes restore these files and in-memory task/audit data. If rollback cannot finish, the journal is retained and further Qt mutations are blocked. Pending transactions are recovered before loading the workspace on startup or refresh; malformed journals fail closed and require inspection. Before startup recovery rolls back, any changed in-flight files are copied to `meta/updates/qt-xp-recovery-<timestamp>-<sequence>/` with a manifest; the folder is excluded from cloud sync and its name is shown in the status bar and local application log. This preserves bytes from a transaction interrupted while writing, but does not prevent external writes from racing an operation while it is active. Do not open the Qt workspace in ImGui or edit its files during an operation.

The full-profile journal is only for failed/interrupted transactions. Successful tasks retain the existing XP rollback snapshot for future task-deletion migration. The stable ImGui transaction code remains untouched.

## Verification

`build-qt.ps1 -Package` builds the Qt client and executes `smoke_qt` plus the existing `smoke_core`. Qt tests use temporary workspaces and exercise loading, search, status filtering, HTML escaping, administrator login/logout, project/task forms, status persistence, keyboard navigation and byte-preserving profile viewing. XP tests cover form cancellation/validation/completion, exact modifiers and rounding, zero XP, blocked profiles, duplicate awards, failed second-profile/task/audit writes, byte-exact rollback, simulated restart recovery and rejection of unsafe recovery paths.

Profile tests additionally exercise creation, edit-save failure, preservation of XP/wallet/credentials, archive cancellation/restore, password confirmation and current-password validation, and the administrator entry point.

For visual QA, run `smoke_qt.exe -platform windows` with Qt `bin` on PATH and `FORGEMIRROR_QT_TEST_ARTIFACTS` set to a disposable output directory. It captures the real XP dialog at normal and minimum sizes. The offscreen platform is appropriate for interaction tests but may render missing font glyphs on Windows.

For a packaged startup check (no installed Qt on PATH):

```powershell
.\package-qt-next\ForgeMirrorQt.exe --storage-dir Z:\CPP\ForgeMirror\build-qt\runtime-test --smoke-test --screenshot Z:\CPP\ForgeMirror\build-qt\qt-window.png
```

The screenshot/smoke flags are development diagnostics. They never default to the production workspace.

### Project and skill editors (stage 4)

Administrators can select a project or catalog skill and use **Редактировать**. Project editing preserves the ID and creation time; task rows resolve the current project name by ID without rewriting historical task snapshots. Deletion is not exposed in this increment.

Skills support name, description, category and weight (0.5–1.6). Renaming preserves the skill ID, profile files and existing profession links. Existing profile XP and weights are not recalculated. Descriptions are required and single-line because the legacy text format cannot safely encode arbitrary multiline fields; pipe and control characters are rejected. Stage 9 adds profession selection for new and existing skills.

The legacy catalog writer does not return an I/O result. Qt therefore edits a temporary copy, reloads and compares every record, then uses QSaveFile with direct-write fallback disabled to replace skills.txt atomically. A changed source or failed verification/write leaves the original intact. Stage 9 fixes reading category and profession together; unsupported or lossy round trips are still rejected. Do not edit the workspace externally during a save.

Stage 4 tests cover real editor validation/creation/cancellation, duplicate and unsafe input rejection, a target-path I/O failure, stable skill IDs, unchanged profile bytes, profession preservation, rejection of lossy serialization, pending recovery, and project rename identity plus linked-task display.
### Task metadata editor (stage 5)

On Tasks, administrators can select a row and use **Редактировать**. The existing form now edits title, description, project, priority, category, pipeline stage, deadline, penalty, assignees and skills. Status changes remain a separate workflow. Changing text requires a nonempty description, following the domain service. Existing missing/archived references remain selected; opening and saving does not silently drop them. Existing selection order is retained.

After XP has been awarded, category, penalty, assignees and skills are locked in both the form and transaction service. ID, creation time, status, score, XP amounts, participants and rollback snapshots are never assigned from editor input. Updating other metadata does not recalculate XP.

EditTaskDetails invokes existing mutation/audit services inside the shared recovery journal. The task-edit manifest uses FORGEMIRROR_QT_TASK_EDIT_1 with exactly the tasks, audit and last-good task files; existing XP manifests keep their original validation. Any failed operation restores all three files and the in-memory collections. A blocked rollback leaves the journal in place, prevents further mutations, and is retried on reload/startup. Avoid external writers during a transaction. This protects process interruption, not arbitrary disk failure.

Tests include the actual edit form, cancelled correction, locked XP fields, primary write failure, a real Windows audit-file sharing violation, failure after earlier fields were saved, byte-exact rollback, restart recovery, and preservation of awarded XP/snapshots.
### Pipeline definition editor (stage 6)

Administrators can create or edit stages from Pipeline. Four compact tabs expose basic metadata, input/output and completion checks, next-step links, risks, historical notes and hints. IDs are never editable. New stages receive a unique ID only when saving the complete form; cancellation leaves no placeholder. Existing missing links remain selected and retain their order unless explicitly removed. Cycles between existing stages are permitted; this form does not impose a new DAG policy on legacy workflows.

A complete candidate collection is saved once through AppSavePipelineCandidate, which holds the shared workspace lock and validates the dialog-opening snapshot against the latest normalized disk state before its atomic replacement. Memory is updated only after success; a stale dialog refreshes workspace data and must be reopened. Pending task/XP recovery blocks saving. Task and profile files are not rewritten when editing definitions; task rows resolve the current stage title by ID. Guided progression uses validated next-step transitions (stage 57 and later); checked deletion and atomic reordering are also implemented.

Tests exercise creation, cancellation, empty-title validation, injected primary-write failure, preservation of IDs/links/hints/notes, multiline criteria, and the administrator entry point with a linked task. All four tabs were inspected at the 520x440 minimum size on Windows.
### Guided task transitions (stage 7)

On Tasks, administrators can use **Следующий этап** while the pipeline module is enabled. The dialog shows the current completion criteria, destination description/input/owner, and requires explicit readiness confirmation. Changing the destination clears confirmation. Only existing nextIds are offered, without duplicates or self-links. Missing/unassigned stages and terminal stages explain why no transition is available. Completed tasks must first be reopened through the separate status workflow.

AdvanceTaskPipeline rechecks the source ID, target, edge and unambiguous stage IDs before using the existing transactional metadata service. It changes only the stage, logs the transition, and leaves status and XP unchanged. Readiness is a human confirmation, not automated verification of the criteria. Manual stage assignment remains available to administrators in the task editor for initial assignment and corrections. External concurrent edits remain unsupported; refresh after editing files outside the app.

Tests cover allowed/disallowed/stale/missing/self/terminal/completed transitions, choice deduplication, readiness gating, cancellation, failure rollback, audit persistence and administrator-only entry. The dialog was inspected at 480x360; native Windows tests and packaged startup without installed Qt on PATH passed with empty stderr.
### Personal access foundation (stage 8, extended in stage 17)

The overflow menu offers **Войти в выбранный профиль** / **Выйти из профиля**. This follows the legacy selected-profile password flow: public profile/task browsing remains read-only, and personal unlock never grants administrator actions. Normal password change now requires both an unlocked session and the existing current-password check. Administrators retain their separate password-reset route in profile management.

QtProfileSession keeps the profile ID and a SHA-256 credential fingerprint in memory. Each render and protected password action checks the current credential plus blocked/archive/readability state. External changes become visible when data is checked/refreshed, not through a background watcher. Stage 17 optionally persists only the ID and expiry described below.

This is a local UI access gate, not encryption or a security boundary against someone who can edit application files. The legacy password encoding is unchanged.

Tests cover incorrect/empty passwords, fresh-session denial, explicit logout, profile mismatch, password replacement, blocking/archive, UI login validation, non-escalation to administrator, and unchanged profile bytes during browsing/login.
### Professions and skill bindings (stage 9)

Administrators can create and rename professions, keeping their IDs. The form rejects duplicate names, pipe characters and control/newline characters because professions.txt is a line-based format. It invokes the existing domain serializer in a temporary directory, verifies all records after reload and atomically replaces the destination via QSaveFile. Failed writes leave the original file and in-memory collection unchanged. Cancel creates nothing. Do not modify the workspace externally while editing.

The skill editor offers multiple checked professions, retains existing unknown IDs until explicitly removed, and shows profession names in the catalog table (also searchable). All skill fields and bindings are saved together through the existing checked atomic path. Profile files, accumulated XP and profile skill weights are not rewritten by ordinary skill edits.

### Profession deletion (stage 32)

Administrators can delete a uniquely identified profession after a confirmation that reports linked active profiles and skills. The operation removes the profession from `professions.txt`, active profile assignments and skill bindings together. A durable journal stores the original profession catalog, skill catalog and every active profile before the first write; startup restores the exact files after an interruption, while a normal completion commits the journal only after all writes succeed.

Deletion is blocked when the profession is still assigned to an archived profile. Restore that profile and remove or change its profession first; Qt does not silently rewrite archived user data. Duplicate profession IDs, unsafe profile IDs, links in journal paths and malformed or incomplete journals fail closed. Tests cover interrupted recovery, committed deletion, affected-record counts, cleared bindings and the real confirmation/button path.

Two shared loader corrections are included only in the migration branch: SkillCatalog consumes both leading cat/prof metadata tokens in either order, and LoadProfessionsData strips the UTF-8 BOM before reading the first ID. The storage formats themselves are unchanged. Existing malformed IDs already embedded in profile/skill records are not rewritten automatically; unknown references remain visible for review. The stable branches and existing installer remain unchanged.

Tests cover profession creation/edit/cancel, unsafe input, failed destination write, BOM-safe ID round-trip, category+profession metadata order and description preservation, binding addition/removal, unknown-link retention and rejection of new missing links. Both forms were visually inspected; native tests and packaged startup without installed Qt on PATH exited 0 with empty stderr.

### Skill deletion (stage 33)

Administrators can delete a uniquely identified skill only when it is unused. References from tasks, active or archived profile skill progress, and achievements block deletion; Qt reports active relationship counts and requires archived profiles to be restored before their data can be changed. Accumulated XP and historical task links therefore cannot vanish through a catalog action.

The operation journals `skills.txt` before writing, reloads the resulting catalog and compares IDs, order, names, descriptions, categories, weights and profession bindings. A locked destination, lossy serialization, interrupted process or malformed journal cannot be committed as success. Tests cover all relationship guards, Windows destination locking, restart recovery, committed deletion, in-memory profession-map cleanup and the actual Qt confirmation/button path.
### Achievement viewing and granting (stage 10)

The Profile page offers **Достижения**, with skill, bonus, expiry and active/expired state. Viewing remains public, consistent with legacy profile browsing. Only administrators see **Выдать достижение**. Grants require an existing catalog skill and an active, unblocked profile, a nonempty title, bonus 0–10000%, and duration 0–36500 days (0 means permanent). Duplicate achievements are allowed, as in the existing additive bonus model.

The Qt grant path atomically appends to achievements/<profile-id>.json via QSaveFile with direct-write fallback disabled. Existing JSON objects and unknown fields are retained; malformed input is rejected instead of overwritten. It never calls save_profile, whose legacy sidecar writer does not report failures. Profile INI bytes, accumulated XP, wallet and task history remain unchanged. Pending task recovery blocks grants. External simultaneous writers are unsupported.

The existing Profile::skill_bonus_multiplier continues to determine active bonuses, and task completion already uses it. No recalculation is performed for past XP. Stage 11 adds editing and revocation; stage 12 adds the local icon picker. Tests cover form validation/granting, read-only viewing, expiry and additive multipliers, malformed JSON, destination failure, pending recovery, blocked profiles, and unchanged profile bytes. Native Windows tests and packaged startup without installed Qt on PATH passed.
### Achievement editing and revocation (stage 11)

Administrators can edit the selected achievement's title and bonus, or revoke it after confirmation (No is the default). Skill, icon, unknown JSON fields and issuance date are preserved. The duration stays unchanged unless **Изменить срок от даты выдачи** is checked; an explicit duration is counted from the original issuance date, with 0 meaning permanent. Existing missing skill references remain intact.

Edits and revocation compare the entire sidecar against the snapshot shown in the list and reject stale selections. Writes use the same atomic QSaveFile path as grants, without rewriting profile INI, wallet, accumulated XP or task history. Revocation affects future bonuses only; it does not reverse past XP. Concurrent external writers remain unsupported.

Tests cover stale snapshots, a real Windows file-sharing write failure, unchanged profile bytes, explicit duration changes, ordinary edits preserving expiry/issuance, and both refusal and confirmation of revocation. The editor was visually inspected on Windows.
### Achievement icons (stage 12)

Grant and edit forms offer thumbnail selection from the workspace's achievements/icons folder and **Без иконки** to clear the reference. The list renders a small icon beside each title. Stored paths retain the legacy achievements/icons/<filename>.png format. No imports, external paths, cloud writes or source image modifications are performed.

Only readable PNG files up to 4 MiB and 2048x2048 are offered. Symlinks and paths outside that folder are not loaded. Missing legacy references remain selected and are preserved on save until explicitly replaced or cleared. Icon validation is repeated during save; stale-sidecar and atomic-write guards remain in effect. An unchanged bonus retains its original precision even if the editor displays fewer decimals.

Tests cover selecting icons during grant/edit, list thumbnails, invalid/missing/traversal paths, malformed images, preserving a missing old reference, cancellation, clearing, and unchanged XP/profile bytes and expiry.
### Personal wallet operation (stage 13)

The profile metrics now include the wallet balance. After a session-only personal login, a profile carrying the Evil Spirit can use **Снять Злого духа · 200** when the wallet has enough coins. The action is hidden without personal access and confirmation defaults to No. Administrators do not gain this personal action merely by entering administrator mode.

The existing AppRemoveEvilSpiritForCoins service performs the operation: it saves the updated profile, adds 200 coins and a spirit_cleanup entry to the local vault, and restores the original profile if the vault write fails. Pending Qt task/XP recovery blocks the action. No cloud wallet push is performed.

Tests drive the actual personal login, cancellation and confirmation, then verify the 250 to 50 wallet change, spirit removal, 200 coin vault credit and log entry. Existing core tests cover insufficient funds and failed-vault rollback. The profile screen was inspected on Windows at the normal application size.
### Local Pomodoro timer (stage 14)

A dedicated Pomodoro navigation page provides a local 25/5/15 minute timer with four focus intervals before a long break. It supports start, pause/resume, reset, a progress indicator, and explicit manual confirmation before each next interval. The timer object stays alive while navigating between Qt pages, but its state is intentionally session-only.

This increment does not award coins, play sounds, auto-advance or persist settings. Those paths depend on personal access, vault schedule rules and settings persistence and will be connected separately rather than bypassed. The page follows the existing palette, keeps one primary action, and uses two compact functional panels.

Tests deterministically cover countdown, pause stability, resume, normal break, long-break selection after two configurable test cycles, reset, and the actual navigation-page visibility. Native Windows rendering was inspected; no timer sleep is used in tests.
### Pomodoro settings and guarded rewards (stage 15)

The Qt page reads the existing [pomodoro] keys from meta/ui.ini. Focus, break, long-break duration, cycles before long break and manual/automatic transition mode can be saved. Saving uses QSaveFile without direct-write fallback and updates only these keys; unrelated sections and unknown lines are retained. Changes apply to the next interval, or immediately while idle.

A fully completed focus can award the vault-defined number of coins. The selected profile must have an active personal session; the focus duration must meet pomodoro_min; its start must fall within pomodoro_start/pomodoro_end on a day enabled by pomodoro_days; and pomodoro_coin must be positive. The existing AppAdjustProfileWallet persistence path saves the reward. A paused/resumed full focus remains eligible; reset and incomplete intervals never invoke the reward. No cloud push occurs.

Tests cover denial without personal login, successful +1 wallet persistence after login, schedule/minimum settings, deterministic full-cycle completion, unrelated ui.ini preservation, atomic settings output, auto-advance and existing wallet/spirit behavior after the reward. Sound selection and playback remain pending.
### Pomodoro sounds (stage 16)

Administrators can enable end-of-interval sounds, choose separate focus and break signals, and set volume. An empty selection uses the system signal. Other choices are legacy-format music/<filename> references discovered only from the isolated workspace music directory. WAV and MP3 files must be regular non-symlink files no larger than 20 MiB; absolute paths, traversal, nested paths and unsupported extensions are rejected. Missing legacy music references remain visible for review but cannot escape the music directory.

The four sound keys are written through the same checked atomic ui.ini update. Non-administrators do not see the sound controls. On Windows playback uses the existing MCI backend with a Unicode path and explicit volume; a missing or unsupported file reports a warning without preventing interval completion or its independently validated reward. Other platforms use the system signal.

Tests cover administrator visibility, local sound discovery, rejection/removal of an external legacy path, exact persisted relative path, preservation of unrelated settings, and the existing atomic-write failure case. The expanded administrator layout was inspected on Windows.

### Trusted profile access and audit (stage 17)

The profile login offers session-only access or local trust for 30/90 days. Trust reuses the legacy `[profile] trusted=id:expiry` entry in `meta/ui.ini`; no password or credential fingerprint is written to disk. Expired entries are pruned. Explicit logout, a detected password/state change, and a successful password change remove the selected profile's trust. Switching the visible profile does not delete another profile's unexpired grant.

Updates preserve the BOM, unrelated sections and unknown lines and use `QSaveFile` with direct-write fallback disabled. Symlinked metadata paths are rejected. Simultaneous external writers remain unsupported. A failed trust write fails the trusted login rather than pretending persistence succeeded. This local convenience is not encryption: anyone able to edit the workspace files can also edit its trust list.

Successful password and trusted unlocks, logout and password changes append to the existing `meta/profile-audit.log`. The administrator Audit page shows the latest 500 profile events together with task audit rows and labels their sources separately. It treats log fields as table text. This is a bounded viewer, not log rotation or tamper protection.

Tests cover 30-day persistence, restoration in a fresh session, explicit revocation, expiry pruning, password fingerprint invalidation, audit output, login choices and the merged administrator table. Native Windows screenshots for the login and audit page were inspected; packaged startup without installed Qt on `PATH` exited 0 with empty stderr.

### Project deletion with rollback (stage 18)

Administrators can delete the selected project after a warning that names it and counts linked tasks. No is the default. Linked tasks are preserved and both their project ID and legacy project-name snapshot are cleared; each detachment is written to task audit. Awarded XP, participants and task status are untouched. A pending Qt recovery journal blocks the operation.

Before the first mutation, Qt now snapshots projects, tasks, their last-good backups and task audit into the shared recovery directory. The journal is atomically marked complete only after every checked write succeeds. A process interruption before that rename restores the complete previous snapshot during startup or refresh; an interruption after the rename cannot undo the committed deletion.

Project and task collections are also restored immediately in memory and on disk if either primary save or the audit append fails. The pre-operation audit cache and file bytes are restored as well; an incomplete rollback leaves the durable journal for the next startup instead of claiming success. This guards process interruption and checked write failures, not arbitrary disk loss or external concurrent writers.

Core tests cover successful detach persistence and forced audit failure with project/task/audit rollback. Qt tests additionally simulate interruption after all mutation writes but before the commit rename, verify restoration of every file on reload, then verify that a committed deletion stays deleted. The UI smoke test drives both cancellation and confirmation, verifies the task becomes projectless, and captures the real warning on Windows.

### Crash-safe project deletion (stage 24)

Stage 24 promotes project deletion from ordinary rollback to restart-safe recovery using manifest `FORGEMIRROR_QT_PROJECT_DELETE_1`. Its five-entry allowlist is strict: projects, tasks, both last-good backups and task audit. Duplicate, missing, linked or unexpected paths fail closed. Other XP and task-edit manifest formats cannot smuggle project files into their broader participant-file journal.

### Crash-safe task creation (stage 25)

Task creation now uses the same three-file metadata journal as task editing: tasks, task audit and the task last-good backup are snapshotted before the first write. A failed task save or audit append restores disk bytes and the in-memory task/audit collections. If the process stops after saving any subset but before the journal commit rename, the next Qt startup or refresh restores the previous snapshot.

The existing task form, validation and domain mutation remain unchanged; this stage only closes their persistence boundary. Tests inject a real post-task audit failure, require byte-identical restoration and confirm no pending journal remains after a successful rollback. The full UI smoke continues to create a task through the actual modal, so the production entry point is covered rather than merely testing a helper.

### Crash-safe task status changes (stage 26)

Ordinary task transitions that do not open the XP completion dialog now share the same metadata recovery boundary. This covers New to In progress, In progress to New, reopening a completed task to In progress, and closing a task whose XP was already awarded. The workflow service still owns transition validation; the Qt wrapper only makes its task save and audit append one recoverable operation.

The status picker is now an explicit localized Qt dialog with `Применить` and `Отмена`, avoiding platform-default English button text while retaining the compact single-choice layout.

Before mutation, the current tasks, audit and last-good task backup are journaled. Audit failure after a successful task save restores exact file bytes and both in-memory collections. An interrupted uncommitted transition is recovered at startup or refresh. Tests inject the audit failure at that exact boundary; the existing UI smoke drives a successful status selection and verifies persisted status.

### Safe task deletion before XP (stage 27)

Administrators can delete a uniquely identified task that has no stored XP participants. Confirmation defaults to Cancel and states that deletion is audited. The same three-file metadata journal protects the task collection, audit and last-good backup; an audit failure after task persistence restores exact disk bytes and in-memory state, while an interrupted uncommitted operation is recovered on startup or refresh.

Tasks with stored participants are deliberately blocked even when their recorded XP values are zero. Their rollback snapshots affect profile XP, levels, counters, skills and category history, so deleting them safely requires a journal that includes every participant profile. The legacy ImGui sequence performs those saves separately and is not reused as a false transaction. Tests cover cancellation, confirmed UI deletion, forced audit failure, ambiguous IDs and the awarded-task guard; the native warning dialog is captured for visual QA.

### Guarded XP rollback metadata (stage 28)

New XP completions store a versioned rollback envelope for each participant. It contains the existing task-related snapshot from immediately before the award plus a canonical snapshot of the expected state immediately after it. Both payloads use the existing reversible encoding inside the already encoded task JSON field; no password, wallet or unrelated profile metadata is added.

Before an awarded-task deletion may restore the earlier snapshot, Qt requires the participant's current task-related state to match the stored postcondition semantically after normal FileStorage/Profile canonicalization. Later global XP, skill XP, task counters, category scores/cooldowns, recovery state or skill-list changes make the comparison fail and protect that newer progress. Wallet, credentials, achievements, profession and spirit are outside the task snapshot and are not overwritten by rollback.

Legacy snapshots remain readable by the existing apply helper but have no postcondition, so they cannot pass the new safety check. Tests verify envelope persistence, canonical postcondition matching, rejection after later progress, and restoration of the pre-award profile state.

### Guarded awarded-task deletion (stage 29)

Administrators may now delete a task awarded by the Qt-v2 completion path and roll back its participant profiles when every current profile still matches that task's stored postcondition. The confirmation names the destructive XP rollback. Missing, archived, duplicate or changed participants, malformed envelopes and legacy snapshots fail before the journal or any file mutation begins.

The existing XP recovery journal snapshots every participant INI together with tasks, task audit and the task last-good backup. Profile rollback saves occur first, followed by audited task deletion; only then is the journal atomically marked complete. A profile-save, task-save or audit failure restores all participant files plus task/audit memory and disk. Process interruption before commit is recovered on startup, while a completed deletion is not resurrected.

Tests cover a forced audit failure after profiles and tasks were already written, byte-safe recovery to the post-award state, rejection after later progress, successful UI deletion and restoration of pre-award XP/task counters. The legacy ImGui rollback sequence remains unchanged and legacy awarded tasks stay protected rather than risking newer progress.

### Management report CSV export (stage 30)

The administrator Statistics page now exposes **Экспорт CSV**. It builds a fresh local `TeamValueReport` at click time and writes the existing summary, project and assignee sections. The file uses UTF-8 with BOM so Cyrillic project names open predictably in common Windows spreadsheet tools. Cloud services are not invoked.

The save dialog is an explicit compact Qt dialog with a timestamped `.csv` suggestion and automatic extension. Output is staged through `QSaveFile` with direct-write fallback disabled, so a failed replacement leaves an existing destination intact. Empty paths and directories are rejected. Tests cover BOM/Cyrillic/comma quoting, invalid targets, a real Windows sharing lock, and the actual Statistics-page dialog/action. Native QA captures the report table with its export control.

### Employee report view (stage 31)

Statistics now switches in place between **По проектам** and **По сотрудникам** without adding another navigation page. The employee table shows the current profile name plus stable ID, active/completed/overdue/pending-XP counts and both global and skill XP. Missing historical profile IDs remain visible as their ID rather than disappearing. The existing section search filters names and IDs in either view.

The summary reports the number of employees represented in tasks, unassigned work and issued global XP. The selector is visible only on Statistics and preserves the compact single-row filter layout. Tests use the real report service and profile fixture, switch the actual combo, verify all eight columns and resolved name/ID, then capture the native employee view.

### Pipeline-stage deletion (stage 19)

Administrators can delete a uniquely identified unused pipeline stage after confirmation. Stages referenced by tasks are blocked until those tasks are moved or detached; duplicate IDs are also blocked rather than guessed. Removing a stage deletes every inbound occurrence of its ID from other stages' `nextIds`, while unrelated and missing legacy links remain unchanged.

The complete candidate pipeline is persisted once through the existing atomic recovery writer. A failed primary replacement restores the in-memory collection and leaves the previous pipeline on disk. No task file is rewritten because linked stages cannot enter this operation. A pending Qt recovery journal blocks deletion.

Core tests cover duplicate inbound-link cleanup, preservation of unrelated links and forced primary-write rollback. Qt tests drive cancellation, successful deletion and rejection of a task-linked stage. The confirmation dialog is captured and inspected on native Windows.

### Pipeline ordering (stage 20)

Administrators can move a uniquely identified stage one position up or down. Boundary actions are disabled, and ambiguous or empty IDs are rejected. The pipeline table deliberately disables column sorting so its visible order remains the persisted workflow order; other tables keep their existing sorting behavior.

Each move swaps the two complete stage records and saves the full collection through the existing atomic recovery writer. IDs, task references, next-step links and stage contents are not rewritten. A failed replacement restores the original in-memory and on-disk order. Pending Qt recovery blocks reordering.

Core tests cover successful persistence and forced-write rollback. The Qt smoke test drives both directions, checks selection-aware button states and confirms that table sorting cannot disguise the workflow order. The reordered table is captured on native Windows.

### Gameplay rules (stage 21)

Administrators receive an F4 Rules page with the current level curve, category base XP, focus bonuses, repeat/recovery factors and warmup-task count. Editing uses bounded integer and decimal fields. The page states explicitly that saved values affect future calculations and do not silently rewrite accumulated profile progress.

Qt serializes the candidate through the existing GameplayConfig implementation in a temporary directory, reloads and compares every supported field, checks the UTF-8 BOM, then replaces `meta/gameplay.ini` using `QSaveFile` with direct-write fallback disabled. Symlinked metadata targets and pending Qt recovery are rejected. A failed commit leaves the previous file and active configuration unchanged. Unknown legacy keys are not preserved because the canonical core serializer does not support them.

After a successful commit, the sanitized rules become the active process configuration and the workspace snapshot is refreshed. Bulk profile recalculation remains unavailable until it can use a crash-safe cross-file journal. Tests cover real form persistence, runtime application, canonical reload and a Windows sharing violation with byte-identical preservation. The editor and summary page were inspected natively.

### Qt display settings (stage 22)

The overflow menu exposes local Qt display settings to every user. Text scale can be set to 90, 100, 110, 125, 150, 175 or 200 percent, and table rows can switch between the normal 28px and compact 24px density. Both changes apply immediately. The migration palette remains fixed, following the recorded design decision to preserve existing colors.

Settings use a dedicated `[qt]` section in `meta/ui.ini`. The writer preserves the BOM, unrelated sections and unknown lines, rejects symlinked metadata paths and replaces the file through `QSaveFile` without direct-write fallback. This coexists with trusted-profile and Pomodoro settings rather than overwriting them. Simultaneous external writers remain unsupported.

Tests cover dialog persistence, reload, live font application, compact row application, preservation of unrelated bytes and a real Windows sharing violation. The 125% dialog and compact main table were inspected natively without clipping.

### Keyboard shortcuts (stage 23)

The Qt window now provides shortcuts only for actions already implemented safely in the current page: Ctrl+N creates, Ctrl+E edits the selection, Delete removes a selected project or unused pipeline stage, Ctrl+R reloads the isolated local workspace, and Ctrl+I toggles details. Existing F1–F6 navigation remains unchanged. Ctrl+/ and the overflow-menu action open an in-app reference table.

These bindings do not bypass context, selection or access checks. Unsupported pages remain unchanged, mutation shortcuts still require administrator login, and deletion keeps its existing confirmation and relationship checks. Modal dialogs block the main-window shortcuts while open. Tests verify every binding, the help-table contents and the functional details toggle; the help dialog is captured during native Windows visual QA.

### Keyboard search and table context (stage 129)

`Ctrl+K` moves focus to the current section search and selects its text; `Esc` clears a non-empty query while that field has focus. Both commands appear in the keyboard-help dialog. Navigation, search, and the main data table expose accessible names; the table description reports visible row count and column headers after each render. Tests exercise the actual shortcut events and verify the accessibility metadata. This is a focused increment, not a complete screen-reader or high-contrast audit.

### Wallet transaction outcomes in core audit (stage 130)

The existing journaled wallet mutation path now emits a bounded local core event after commit, rollback, a recovery block, or an unrecoverable recovery attempt. These events use fixed generic messages and omit profile/task identifiers, amounts, memos and exception text. Administrators see them in the existing **Core-событие** audit source, alongside Qt XP transaction outcomes; other users cannot select or export that source. The integration test exercises a committed Pomodoro reward and an injected audit failure that rolls back, then verifies both outcomes in the admin audit table.

### Manual cloud transfer outcomes in core audit (stage 131)

Confirmed manual pull and push operations now report fixed generic outcomes to the existing bounded local application log. Success, unchanged pull, failure, completed rollback and pending recovery are distinguished without recording cloud paths, filenames, file contents, secrets or exception text. The administrator-only **Core-событие** audit source includes these entries, separate from ordinary Qt activity. UI tests verify successful transfers and ensure configured cloud paths do not appear in the local event text. Cancelled confirmations and read-only previews do not produce transaction events.

### Recovery outcomes in core audit (stage 132)

On startup or workspace refresh, successfully recovered local transaction, manual cloud-pull, and manual cloud-push journals now produce fixed generic events in the bounded local log. The admin-only core audit source classifies these as transaction recovery. Local transactions use one shared journal for XP, wallet, profile, skill, project, rules and related operations, so the event intentionally does not guess which domain action was interrupted. Pull and push recovery are identified by transaction type only; paths, filenames and file contents remain out of the log. A malformed or unrecoverable journal still blocks workspace loading and cannot emit a normal recovery event until it is resolved.

### Rules and direct-XP outcomes in core audit (stage 133)

Successful or failed direct skill-XP grants and rules recalculation now emit fixed, generic profile-core outcomes to the bounded local log; unfinished recovery is reported separately. Events contain no profile or skill IDs, XP values, or free-form errors. Administrators can inspect these entries in the existing **Core-событие** audit source. The UI regression test exercises both committed operations and verifies their generic log text and audit classification.

### Accessible names for dense filter toolbars (stage 134)

Task, report and audit filters now expose explicit Russian accessible names to assistive technology, independent of their compact placeholder-only visual presentation. The selected profile and catalogue profession filter are also labelled. The regression test checks each filter group on its corresponding page; table headers and the existing navigation/search descriptions remain intact. This is a focused keyboard/screen-reader metadata pass, not a full assistive-technology audit.

The full-push confirmation comparison now treats the automatically generated `dataUpdatedAt` value as volatile while requiring every other manifest field, all before-images, and every other after-image to match the approved preview. This prevents a false stale-preview rejection when the two previews straddle a wall-clock second. The recovery smoke deliberately waits across that boundary and verifies that the local and cloud task bytes remain unchanged until the transaction starts.

Stage 134 verification: `build-qt.ps1 -Package` succeeded; the Qt smoke passed 1/1 and `smoke_core` reported OK. The smoke was also repeated three consecutive times with the forced manifest-second boundary. Packaged `--smoke-test` exited 0 with Qt removed from `PATH`. The portable package is a QA output only; no installer/release was produced for this implementation checkpoint.

### Archived profile cleanup on profession deletion (stage 135)

Qt profession deletion now includes matching archived profiles instead of refusing the operation. It temporarily restores only archived profiles that use the selected profession, clears their profession binding, and returns them to the archive. Active and archived profile files, including both possible locations during the temporary move, are captured in the existing recovery journal. A process interruption between moving an archived profile and saving it restores the archived file and removes a stray active copy. The shared service keeps its legacy default behavior for the stable ImGui caller; Qt opts into archived-profile handling explicitly. Smoke tests cover interruption recovery and successful deletion across both profile states.

Stage 135 verification: the focused archived move-recovery and profession deletion cases pass inside `smoke_qt`; the full suite passes 1/1, and `smoke_core` reports OK. `build-qt.ps1 -Package` succeeded, and packaged `--smoke-test` exited 0 with Qt removed from `PATH`. This remains a portable QA build, not an installer release.

### Profession merge (stage 136, implementation checkpoint)

Editing a profession to an existing name asks for explicit confirmation and defaults to Cancel. Confirmed merge transfers active and archived profile assignments to the destination profession, redirects and deduplicates skill-catalog bindings, retains the destination ID, applies the entered destination description, and removes the source record. Profile and metadata writes are protected by the existing recovery journal; both catalog files are staged and verified before the live replacement. The Qt smoke test covers confirmation, active and archived assignments, duplicate skill-binding collapse, and interrupted journal recovery. ImGui's implementation and default behavior remain unchanged.

Stage 136 verification: `build-qt.ps1 -Package` succeeded, `smoke_qt` passed 1/1, and `smoke_core` reported OK. Packaged `--smoke-test` exited 0 with Qt removed from `PATH`. This remains a portable QA build, not an installer release.

### Report group drill-down (stage 137, implementation checkpoint)

Selecting a project or employee in Statistics now shows the group composition rather than just a flat task list. The detail panel includes new/in-progress/done counts, overdue and XP-pending totals, XP scoped to the selected employee or project, and per-task status, priority, creation/deadline, resolved project and pipeline stage, involved profiles and XP. It uses the same creation-date period as the aggregate table and preserves the selected report grouping. Qt smoke asserts that task text is rendered as text and the selected employee's task/stage appear in the drill-down.

Stage 137 verification: `build-qt.ps1 -Package` succeeded, `smoke_qt` passed 1/1, and `smoke_core` reported OK. Packaged `--smoke-test` exited 0 with Qt removed from `PATH`. This remains a portable QA build, not an installer release.

### Report period comparison (stage 138, implementation checkpoint)

Statistics can compare its selected creation-date cohort against the immediately preceding equal-length period. The comparison is saved in the Qt display context, is disabled for all-time reports, and is available for rolling 30/90-day, prior-year-to-date, and custom periods. Project and employee tables show side-by-side current and previous counts; employees also show scoped XP. The table includes entities present only in the previous period, with current values shown as zero. Drill-down identifies which period each task belongs to and repeats both groups' summary metrics. Task statuses and XP remain current values, not historical snapshots. Smoke covers preference persistence, accessibility naming, previous-period zeros, and comparison drill-down.

Stage 138 verification: `build-qt.ps1 -Package` succeeded, `smoke_qt` passed 1/1, `smoke_core` reported OK, and packaged `--smoke-test --screenshot` exited successfully with Qt removed from `PATH`; `build-qt/stage138-window.png` confirms the real window opened. This is an implementation checkpoint, not an installer release.

### Comparison export (stage 139, implementation checkpoint)

When report comparison is enabled, CSV export now writes separately labelled current and preceding-period report sections with their date ranges. The export reuses the established TeamValueReport schema for each section, so metrics stay aligned with the report service; ordinary single-period export is unchanged. Smoke validates both labels and previous-period project data.

Stage 139 verification: `build-qt.ps1 -Package` succeeded, `smoke_qt` passed 1/1 (including both-period CSV labels and old-period data), and `smoke_core` reported OK. Packaged `--smoke-test --screenshot` opened the real window with Qt removed from `PATH`; `build-qt/stage139-window.png` was created. This is an implementation checkpoint, not an installer release.

### Pipeline-stage report grouping (stage 140, implementation checkpoint)

Statistics can now group current and previous cohorts by pipeline stage, including tasks with no stage and tasks bound to an unknown stage ID. Stage rows follow configured pipeline order; selecting one opens a scoped task drill-down. Stage CSV export mirrors the visible grouping and comparison columns. The saved report grouping accepts the new stage option while existing project and employee indexes remain stable. Tests cover prior-only stage rows, detail filtering, CSV contents and setting round-trip.

Stage 140 verification: `build-qt.ps1 -Package` succeeded; `smoke_qt` passed 1/1 and `smoke_core` reported OK. Packaged `--smoke-test --screenshot` opened the window with Qt removed from `PATH`; `build-qt/stage140-window.png` was created. This is an implementation checkpoint, not an installer release.

### Task-category report grouping (stage 141, implementation checkpoint)

Statistics adds task-category grouping with the same period comparison, previous-only rows, status/XP metrics and scoped task drill-down as stage grouping. Category IDs use the existing clamped 0–4 domain labels. CSV export contains the visible category table and its selected comparison columns. Tests verify previous-only category metrics, drill-down, export-compatible layout and persisted selection.

Stage 141 verification: `build-qt.ps1 -Package` succeeded; `smoke_qt` passed 1/1, including the category table, previous-only group, drill-down and UI CSV export, and `smoke_core` reported OK. Packaged `--smoke-test --screenshot` created `build-qt/stage141-window.png` with Qt removed from `PATH`. This is an implementation checkpoint, not an installer release.

### Legacy awarded task cleanup (stage 142, implementation checkpoint)

When the transactional award rollback cannot be proven safe because a task has a legacy/missing snapshot or a participant profile has since changed, Qt now offers a separate, default-Cancel action to delete only the task record. It leaves each profile file byte-for-byte unchanged and records `xp_disposition` in task audit stating that awarded XP remains in profiles. Task and audit files are covered by the existing recovery journal; failure restores the task record and audit. The ordinary rollback path remains available only when all profile postconditions match exactly. Smoke covers the stale-profile fallback and verifies preserved profile bytes plus audit evidence.

Stage 142 verification: `build-qt.ps1 -Package` succeeded; `smoke_qt` passed 1/1, including legacy/stale award preservation and injected audit failure recovery, and `smoke_core` reported OK. Packaged `--smoke-test --screenshot` exited 0 with Qt removed from `PATH`; `build-qt/stage142-window.png` was created. This is an implementation checkpoint, not an installer release.

### Application-log accessibility (stage 143, implementation checkpoint)

Application-log level filters, source filter, presets, compact/autoscroll options, export and clear controls now have explicit Russian screen-reader names. The clear action also announces that it opens a confirmation and Cancel preserves the journal. The existing log smoke test checks this metadata alongside filtering, export and persistence.

Stage 143 verification: `build-qt.ps1 -Package` succeeded; `smoke_qt` passed 1/1 and `smoke_core` reported OK. The packaged application started with Qt removed from `PATH` using a disposable workspace.

### Administrator profile statistics (stage 144, implementation checkpoint)

The dedicated **Статистика профилей** page restores the legacy ImGui profile analytics without replacing Qt's task/project Statistics report: summary KPIs, top profiles by level/XP/achievements, rank distribution, average category scores, inactivity and recovery lists. Search, archive and rank filters; the inactivity threshold; view; and timed refresh preferences are persisted. The profile reader can inspect active or archived files without changing the currently selected profile or writing normalized data back. CSV export contains filtered profile rows and the existing TeamValueReport section.

Stage 144 verification: `build-qt.ps1 -Package` succeeded; `smoke_qt` passed 1/1 and `smoke_core` reported OK. Focused UI coverage checks byte-preserving active/archive snapshot reads, archived profile KPIs, filters, rankings, category averages, export content, and accessible names. Packaged `ForgeMirrorQt.exe --smoke-test --screenshot` exited 0 with Qt removed from `PATH`, opening the real window against a disposable workspace. This is an implementation checkpoint, not an installer release.

### Personal profile report export (stage 145, implementation checkpoint)

The Profile page now exposes administrator-only TXT and CSV exports matching the legacy profile report: identity, level/rank, total and in-level XP, last activity, recovery count, category scores, and skill level/XP/weight/achievement bonus. Files are written as UTF-8 with BOM using an atomic save; CSV values are escaped, and the user chooses the destination. The export does not write to profile storage or change the saved profile.

Stage 145 verification: `build-qt.ps1 -Package` succeeded; `smoke_qt` passed 1/1, including CSV escaping, TXT/CSV contents and BOM, validation, and admin action availability. `smoke_core` reported OK. Packaged `ForgeMirrorQt.exe --smoke-test --screenshot` exited 0 with Qt removed from `PATH` using a disposable workspace; `build-qt/stage145-window.png` was created. This remains an implementation checkpoint, not an installer release.

### Profile history CSV exports (stage 146, implementation checkpoint)

The profile history dialog can export its event table and task/XP table to separate UTF-8 CSV files with BOM and atomic replacement. Task export follows the active project/task search filter and includes only the rows currently present in the table. Both actions are available to a profile owner after unlock or to an administrator, matching access to the history dialog.

Stage 146 verification: `build-qt.ps1 -Package` succeeded; `smoke_qt` passed 1/1, including event CSV content/BOM and task CSV honoring the active filter; `smoke_core` reported OK. Packaged `ForgeMirrorQt.exe --smoke-test --screenshot` exited 0 with Qt removed from `PATH` using a disposable workspace; `build-qt/stage146-window.png` was created. This remains an implementation checkpoint, not an installer release.

### Extended storage health report (stage 147, implementation checkpoint)

Administrators can export a read-only TXT report with workspace sync-file checks, content-based local/cloud drift, and an inventory of unrecognized storage entries. The comparison does not guess which side is newer when Qt has no last-sync watermark. File contents and cloud paths are not included. Known Qt-owned application log and settings files are excluded from the legacy ImGui stray-file list; unknown Qt files and temporary files remain visible. Export uses UTF-8 with BOM and atomic replacement. No files are deleted or modified.

Stage 147 verification: `build-qt.ps1 -Package` succeeded; `smoke_qt` passed 1/1, covering admin-only action visibility, real UI save flow, UTF-8 BOM, cloud drift, sync-file diagnostics, stray inventory, exclusion of Qt-owned log metadata, source-byte preservation and atomic export validation. `smoke_core` reported OK. Packaged `ForgeMirrorQt.exe --smoke-test --screenshot` exited 0 with Qt removed from `PATH` using a disposable workspace; `build-qt/stage147-window.png` was created. This remains an implementation checkpoint, not an installer release.

### Stage 148 — guarded Qt stray-storage cleanup (implementation checkpoint)

Restored the legacy administrator cleanup capability in the isolated Qt workspace. The UI previews a complete nested inventory with explicit checkboxes, defaults keyboard focus to Cancel, and requires all descendants to be selected before removing a directory. Immediately before deletion the service re-scans and compares metadata and content hashes, rejects stale or unknown approvals, checks the canonical parent remains within storage, refuses reparse traversal, and removes entries individually without recursive-delete APIs. Smoke coverage uses disposable temp workspaces for cancel, selected-only delete, stale inventory, nested-directory approval, and Qt-owned metadata filtering. The portable packaging script now writes `qt.conf` so the deployed `platforms` directory is discoverable in normal startup.

Verification: `smoke_qt` passed 1/1; `smoke_core` reported OK; `build-qt.ps1 -Package` succeeded. The packaged executable exited 0 and created `build-qt/stage148-window-final2.png` with Qt removed from `PATH` and no Qt environment overrides, using only package-local `qt.conf`. No installer lifecycle test was run; `0.6.11` remains the latest lifecycle-verified installer.

### Stage 149 — atomic multi-task deletion (implementation checkpoint)

The Tasks page adds a confirmed **Удалить выбранные** action for multiple valid selected tasks. A single journal covers all task/audit files and affected profile files. The service resolves each profile's selected award snapshots as a unique reverse chain from the current postcondition; then it saves each profile once and deletes the whole selection. If any rollback snapshot is stale, absent, malformed, ambiguous or no longer matches current profile state, it refuses the complete batch before mutation. On write/audit failure it restores exact preimages through startup-recoverable transaction logic. The ImGui branch remains untouched.

Verification: `build-qt.ps1 -Package` succeeded; `smoke_qt` passed 1/1, including UI visibility/confirmation, selected-only deletion, multi-award reverse rollback, stale XP refusal, and injected audit failure with byte-exact restoration of profile/task/audit files. `smoke_core` reported OK. Packaged startup with Qt removed from `PATH` and no Qt overrides exited 0 and created `build-qt/stage149-window-verified.png`. This remains an implementation checkpoint; `0.6.11` is still the latest lifecycle-verified installer.

### Stage 150 — profile-statistics click-through parity (implementation checkpoint)

In ImGui, rows in profile statistics select the corresponding profile on a single click. Qt now matches that behavior for profile-backed views (all profiles, top rankings, inactivity and recovery); rank distribution and category aggregate rows remain non-navigating. Active profiles open on the Profile page. Archived rows retain the existing guidance to open them through profile management. `smoke_qt` verifies the single-click route and selected profile identity.

Verification: `build-qt.ps1 -Package` succeeded; `smoke_qt` passed 1/1 including the click-through assertion, and `smoke_core` reported OK. Packaged startup with Qt removed from `PATH` and no Qt overrides exited 0 and created `build-qt/stage150-window.png`. This remains an implementation checkpoint; `0.6.11` is still the latest lifecycle-verified installer.

### Stage 151 — profile display modes (implementation checkpoint)

The Qt Profile page now mirrors the legacy Overview, Analytics and Focus modes. Overview ranks skills by total XP and shows the top three, Analytics shows the full skill list, and Focus hides the detailed skill table and achievement action while preserving the key profile metrics. The selected mode persists in the Qt display context. Smoke verifies all three visibility states, mode selection/accessibility, and persistence. Existing palette and profile data remain unchanged.

Verification: `build-qt.ps1 -Package` succeeded; `smoke_qt` passed 1/1 and `smoke_core` reported OK. Packaged startup with Qt removed from `PATH` and no Qt overrides exited 0 and created `build-qt/stage151-window.png`. This remains an implementation checkpoint; `0.6.11` is still the latest lifecycle-verified installer.

### Stage 152 — profile skill filters (implementation checkpoint)

The Profile page's Analytics mode now has the legacy skill sort options (name, level, total XP, weight), weight-category and inclusive weight-range filters, a combined reset action, and a total-XP table column. Filter state is stored in the Qt display context. Overview continues to rank by total XP and preview three skills; the filters do not unexpectedly narrow that summary. UI smoke exercises weight ordering, category/range selection, reset, accessible names, and settings round-trip.

Verification: `build-qt.ps1 -Package` succeeded; `smoke_qt` passed 1/1 and `smoke_core` reported OK. Packaged startup with Qt removed from `PATH` and no Qt overrides exited 0 and created `build-qt/stage152-window.png`. This remains an implementation checkpoint; `0.6.11` is still the latest lifecycle-verified installer.

### Stage 153 — achievement list filters (implementation checkpoint)

The Qt achievement dialog now includes title/skill search, an expired-entry toggle, reset, active/expired/visible counts, and a sorted preview of up to three achievements expiring within seven days. Filtering hides table rows without reindexing the backing records; if a filter hides the selected row, edit/revoke selection is cleared. Smoke covers active, permanent and expired entries, upcoming expiry, no-match results, reset, accessibility names, mutation-action selection safety, and byte-exact preservation of profile and achievement data while filtering.

Verification: `build-qt.ps1 -Package` succeeded; `smoke_qt` passed 1/1 and `smoke_core` reported OK. Packaged startup with Qt removed from `PATH` and no Qt overrides exited 0 and created `build-qt/stage153-window.png`. This remains an implementation checkpoint; `0.6.11` is still the latest lifecycle-verified installer.


Stage 133 verification: `build-qt.ps1 -Package` completed, `smoke_qt` passed (1/1), and `smoke_core` reported OK. The packaged `ForgeMirrorQt.exe --smoke-test` exited 0 with a minimal Windows `PATH` that excluded Qt. A later investigation traced an intermittent cloud-push-preview rejection to the generated manifest timestamp; stage 134 records the fix and forced-boundary regression test. No installer was built; this is an implementation checkpoint.

### Stage 154 — profile task dashboard and exact log export filtering (implementation checkpoint)

The Profile page adds a persisted **Задачи** mode with assigned open tasks sorted by overdue state and nearest deadline, overdue-row emphasis, and active/overdue/completed-awaiting-XP counts. Four quick actions open the shared Tasks page scoped to the selected profile and the requested all/active/overdue/XP-pending subset; conflicting status, priority, project, pipeline, age and text filters are cleared. Double-clicking a task opens the same profile-scoped task view. The Tasks mode is unavailable when the task module is disabled. A full-smoke issue also exposed a mismatch between log-table search (which includes the displayed row number) and log export search; export now uses the same row-number-aware search set, so its output matches the visible table.

Verification: `build-qt.ps1 -Package` succeeded; `smoke_qt` passed 1/1, `smoke_core` reported OK, and the packaged executable exited 0 with `PATH=C:\Windows\System32;C:\Windows`, no Qt environment overrides, and created `build-qt/stage154-window-final.png`. This remains an implementation checkpoint; version `0.6.11` is still the latest installer with lifecycle verification.

### Stage 159 — durable administrator login preference (implementation checkpoint)

Saving the administrator password and stay-logged-in preference now writes a complete sibling temporary file and atomically replaces `meta/admin.ini`; an interrupted write cannot truncate the current credential file. The login form reports preference-save errors without clearing the entered password. Regression coverage toggles the persisted flag repeatedly while checking that the password remains unchanged, logs in with the preference enabled, reconstructs a fresh window, verifies restored admin access, logs out and verifies the preference clears, then covers nonpersistent login and password rotation. Existing ImGui and `develop` behavior are unchanged.

Verification: `build-qt.ps1 -Package` succeeded; `smoke_qt` passed 1/1 and `smoke_core` reported OK. No installer lifecycle verification was run; `0.6.11` remains the latest verified installer.

### Stage 160 — palette-preserving interface layout presets (implementation checkpoint)

Qt display settings now control interface spacing and card/accent corner radius alongside text scale and compact table rows. Users can save up to 20 named Qt presets, apply them, update them and delete them explicitly; they are kept in `meta/qt-layout-presets.json` using atomic replacement. Presets already present in the copied legacy `meta/ui-presets/*.ini` directory can be applied read-only. Import copies supported layout values and per-window PNG references/opacity/tiling; legacy themes, custom colors, profile state and credentials remain excluded, and source files are never edited. The Qt palette remains the existing fixed palette.

Verification: `build-qt.ps1 -Package` succeeded; `smoke_qt` passed 1/1 and `smoke_core` reported OK. The packaged executable reported version `0.6.11` and completed `--smoke-test` with `PATH=C:\Windows\System32;C:\Windows`, no Qt environment overrides, exit code 0, and a 40,995-byte screenshot in a disposable workspace. No installer lifecycle verification was run; `0.6.11` remains the latest verified installer.

### Stage 161 — legacy per-window backgrounds in Qt (implementation checkpoint)

Qt display settings now expose an editor for the 18 migrated sections, with per-section PNG selection, clear-all assignment, opacity, tile mode and tile scale. References use the legacy `ui/backgrounds/<filename>.png` form; images are restricted to direct regular PNG files in that directory, reject symlink/reparse entries and traversal, and are bounded to 16 MiB and 32 megapixels before decode. The Qt canvas composites the active section image over the unchanged base palette. Old `meta/ui.ini` background values and read-only legacy layout presets are imported into Qt settings; preset colors and profile secrets remain excluded. Qt writes only its `[qt]` settings while preserving unrelated INI bytes. Tests cover safe listing/loading, traversal rejection, legacy migration, custom-preset round-trip, the nested UI editor, persistence and page binding.

Verification: `build-qt.ps1 -Package` succeeded after the feature changes; `smoke_qt` passed 1/1 and `smoke_core` reported OK. The package was deployed, but a separate visible packaged-client smoke was not confirmed in this turn. No installer lifecycle verification was run; `0.6.11` remains the latest verified installer.

### Stage 162 — status-grouped task reports and workspace-aware admin login (implementation checkpoint)

Statistics now groups the selected creation-date cohort by task status, with the same active/done/overdue/pending-XP and global/skill XP metrics, previous-period comparison, selected-row drill-down and visible-table CSV export as the existing groupings. The login dialog identifies the Qt-local workspace path next to its persistent-session option; Qt credentials and the stay-logged-in flag belong to this workspace and are separate from stable ImGui storage. Smoke verifies repeated window restarts preserve a remembered session and password, shows the workspace hint, and checks status counts, comparison columns, drill-down and preference persistence.

Verification: `build-qt.ps1 -Package` succeeded; `smoke_qt` passed 1/1 and `smoke_core` reported OK. The packaged executable completed `--smoke-test` with `PATH=C:\Windows\System32;C:\Windows`, no Qt environment overrides, exit code 0, and a 39,709-byte screenshot in a disposable workspace. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Stage 163 — privacy-safe task mutation outcomes (implementation checkpoint)

The local Qt application log and administrator Audit core-event source now record confirmed task creation, editing, status changes, single and bulk deletion, and bulk updates, including fixed generic failure/rollback outcomes. Records contain only the operation and, for bulk operations, changed/skipped counts; task titles, task IDs, profile IDs, raw service errors and exception text are excluded. Existing task audit remains the detailed field-history source. Tests cover the real UI paths for create/edit/status/bulk update and inspect the persisted log for expected outcomes and absence of fixture identifiers.

Verification: `build-qt.ps1 -Package` succeeded; `smoke_qt` passed 1/1 and `smoke_core` reported OK. The UI smoke verifies generic task create/edit/status outcomes and privacy, and the bulk-task test verifies its operation/count event. The packaged executable also completed `--smoke-test` with `PATH=C:\Windows\System32;C:\Windows`, no Qt environment overrides, exit code 0, and a 39,699-byte screenshot in a disposable workspace. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Stage 164 — task-priority report grouping (implementation checkpoint)

Statistics now groups current and preceding creation-date cohorts by the four normalized task priorities. Each row uses the existing team-value metrics, including status, overdue, pending-XP and XP totals; selecting a row opens a priority-scoped task drill-down. CSV export reflects the visible priority table, and the selection persists in Qt display settings. Tests cover a previous-period-only priority, comparison columns, drill-down, CSV and settings reload.

Verification: `build-qt.ps1 -Package` succeeded; `smoke_qt` passed 1/1 and `smoke_core` reported OK. The packaged executable completed `--smoke-test` with `PATH=C:\Windows\System32;C:\Windows`, no Qt environment overrides, exit code 0, and a 39,706-byte screenshot in a disposable workspace. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Stage 165 — deadline-state reporting and task-mutation audit classification (implementation checkpoint)

Statistics now groups the selected current and preceding cohorts into completed, overdue, upcoming-deadline and no-deadline rows. The bucket is disjoint and based on current task status/deadline; it is not a historical reconstruction. The standard report metrics, drill-down and CSV export apply. The Audit page now classifies privacy-safe `CoreTaskMutation` events under its administrator-only core-event source instead of ordinary application activity. Tests cover all deadline buckets, a previous-only row, detail/CSV/persistence, and task-mutation event classification.

Verification: `build-qt.ps1 -Package` succeeded; `smoke_qt` passed 1/1 and `smoke_core` reported OK. The packaged executable completed `--smoke-test` with `PATH=C:\Windows\System32;C:\Windows`, no Qt environment overrides, exit code 0, and a 39,691-byte screenshot in a disposable workspace. Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.

### Stage 166 — privacy-safe project and catalog audit outcomes (implementation checkpoint)

Administrator project create/edit/delete and skill, profession and pipeline-stage create/edit/delete/reorder operations now append fixed, privacy-safe outcomes to the bounded Qt log. Create/edit operations record successful saves; deletion and pipeline reordering also record generic failed/rolled-back outcomes. The administrator Audit page classifies them with the existing core-event source as **Изменение справочников**. Messages exclude project/catalog names, IDs, free-form descriptions and service errors; project deletion records only the count of detached tasks. The UI smoke creates a project through the real editor, verifies its generic log outcome does not contain its title, and confirms the event is visible through the core audit filter. Qt admin-session troubleshooting remains workspace-specific: the login dialog displays the exact local path, and no password values are needed for diagnosis.

Verification: `build-qt.ps1 -Package` succeeded; `smoke_qt` passed 1/1 and `smoke_core` reported OK. The packaged executable reported `ForgeMirrorQt 0.6.11` and completed the real-window `--smoke-test` with `PATH=C:\Windows\System32;C:\Windows`, Qt plugin environment overrides removed, exit code 0 and empty stderr; screenshot: `build-qt/stage166-package-smoke-e3a97418bcd5436d9c0c5445c87398b2/window2.png` (40,855 bytes). Installer lifecycle verification was not run; `0.6.11` remains the latest verified installer.


### Stage 167 — persistent application-log filters (implementation checkpoint)

The Qt application log now persists the Info/Warnings/Errors level toggles and selected source alongside compact view and autoscroll. Preset buttons update the same saved settings; a source no longer present in the bounded log is safely reset. Tests exercise INI round-trip and reconstruction of the real window to verify all filters survive restart.

Verification: `build-qt.ps1 -Package` passed; `smoke_qt` passed 1/1 and `smoke_core` reported OK. The installed executable reported ProductVersion/FileVersion `0.6.13`; its real-window smoke exited 0 with `PATH=C:\Windows\System32;C:\Windows`, Qt plugin overrides cleared, empty stderr, and a 40,891-byte screenshot. Install/update/uninstall validation is recorded in `docs/releases/ForgeMirror-0.6.13.md`.

### Stage 168 — persistent application-log search (implementation checkpoint)

The Qt log search query now persists in the Qt workspace, restores when returning to Logs and at startup, and remains scoped to that page; navigation clears it on other pages. Tests cover settings round-trip, real navigation restore/isolation, and a reconstructed window. Existing log entries and the fixed palette are unchanged.

Verification: `build-qt.ps1 -Package` passed; `smoke_qt` passed 1/1 and `smoke_core` reported OK. Log-search round-trip, page-scoped navigation restore and startup restoration passed. The installed 0.6.14 real-window smoke exited 0 with `PATH=C:\Windows\System32;C:\Windows`, Qt plugin overrides cleared, empty stderr, and a 40,910-byte screenshot. Install/update/uninstall validation is recorded in `docs/releases/ForgeMirror-0.6.14.md`.

### Stage 169 — compact reference-led navigation (implementation checkpoint)

The permanent sidebar now uses concise section labels, 18 consistent Qt style icons, keyboard-shortcut hints in tooltips, and a distinct active state with the existing purple accent. Accessible item text remains explicit; the established dark palette is unchanged. No page order or saved page index changed.

Verification: `build-qt.ps1 -Package` passed; `smoke_qt` passed 1/1 and `smoke_core` reported OK. A real-window package smoke exited 0 with minimal Windows `PATH`, Qt plugin overrides cleared and empty stderr; screenshot: `build-qt/stage169-package-smoke-final/window.png` (44,712 bytes). Installer update/startup/uninstall verification is recorded in `docs/releases/ForgeMirror-0.6.15.md`.

### Stage 170 — audited authentication and system maintenance (implementation checkpoint)

The administrator Audit source now also classifies safe outcomes for rejected/successful/restored/ended administrator sessions, storage-health report export and approved stray cleanup, and cloud-release download/launch. Authentication records never contain entered passwords; cleanup records omit paths and filenames. The source option is now named **Core-события** to match its contents.

Verification: `build-qt.ps1 -Package` passed (`smoke_qt` 1/1, `smoke_core: OK`). The packaged real-window smoke exited 0 with minimal Windows `PATH`, Qt plugin overrides cleared and empty stderr. Installer 0.6.16 passed isolated 0.6.15 update, installed startup and uninstall checks; separate user data survived. Exact evidence, installer size and SHA-256 are recorded in `docs/releases/ForgeMirror-0.6.16.md`.

### Release 0.6.12 verification

The stage 166 Qt build was rebuilt with canonical version `0.6.12`, tested (`smoke_qt` 1/1, `smoke_core: OK`) and packaged. The installer was tested in isolation: 0.6.11 install, 0.6.12 in-place update, version/uninstall-registry checks, real-window startup without Qt on `PATH`, and uninstall. Separate user data survived both update and uninstall. Exact paths and SHA-256 are in `docs/releases/ForgeMirror-0.6.12.md`.

### Stage 171 — Qt program information (implementation checkpoint)

The application menu's **О программе** dialog now carries over the stable ImGui screen's product description, author credits and version, with the canonical build version. It also preserves the Qt-specific migration status, isolated-workspace warning, update capabilities and coverage-document pointer. The interface palette is unchanged.

Verification: `build-qt.ps1 -Package` passed (`smoke_qt` 1/1, `smoke_core: OK`). The package window smoke exited 0 with minimal Windows `PATH`, Qt plugin overrides cleared and empty stderr. Installer 0.6.17 passed isolated 0.6.16 update, installed startup and uninstall checks; separate user data survived. Exact evidence, installer size and SHA-256 are recorded in `docs/releases/ForgeMirror-0.6.17.md`.

### Stage 172 — legacy window opacity setting (implementation checkpoint)

Qt now exposes a 60–100% main-window opacity control matching the stable ImGui global-alpha range. The value is persisted in [qt], applied to the main window at startup and after saving display settings, imported from legacy [style] alpha, and carried through Qt layout presets. Modal dialogs remain fully opaque for readability. The fixed palette is unchanged.

Verification: `build-qt.ps1 -Package` passed (`smoke_qt` 1/1, `smoke_core: OK`). Opacity behavior is covered for legacy import, Qt preset save/apply, settings round-trip and restored main-window opacity. Package smoke exited 0 with minimal Windows `PATH`, Qt plugin overrides cleared and empty stderr. Installer 0.6.18 passed isolated 0.6.17 update, installed startup and uninstall checks; separate user data survived. Exact evidence, installer size and SHA-256 are recorded in `docs/releases/ForgeMirror-0.6.18.md`.

### Stage 173 — built-in Qt layout presets (implementation checkpoint)

Qt display settings now include the stable UI's **Минимализм**, **Презентация** and **Компактный** quick presets. They adjust only supported layout controls; the fixed Qt palette and unrelated profile state stay unchanged. Minimalism also carries over its reduced window opacity/background emphasis. Applying a quick preset changes the draft; the user still chooses Save.

Verification: `build-qt.ps1 -Package` passed (`smoke_qt` 1/1, `smoke_core: OK`). Tests verify each preset, preservation of background paths and fields the preset does not own, UI application, and explicit save behavior. Installer 0.6.19 passed isolated 0.6.18 update, installed startup with Qt removed from `PATH`, and uninstall; separate user data survived. Exact evidence, installer size and SHA-256 are recorded in `docs/releases/ForgeMirror-0.6.19.md`.

### Stage 174 — disabled module parity (implementation checkpoint)

Qt now honors the same `FORGEMIRROR_DISABLE_MODULES` switches as stable ImGui for Tasks, Pipeline, Achievements, Shortcuts, Pomodoro, Cloud, 3D and Professions. Disabled pages and profile actions are hidden, a saved selection of a disabled page falls back to Profile, and the Cloud switch suppresses manual cloud operations and automatic synchronization.

Verification: `build-qt.ps1 -Package` passed (`smoke_qt` 1/1, `smoke_core: OK`). The Qt window test disables all supported modules and verifies navigation/action visibility and fallback from a saved Cloud page. Installer 0.6.20 passed isolated 0.6.19 update, installed startup with Qt removed from `PATH`, and uninstall; separate user data survived. Exact evidence, installer size and SHA-256 are recorded in `docs/releases/ForgeMirror-0.6.20.md`.

### Stage 175 — unambiguous administrator session action (implementation checkpoint)

The application menu now says **Войти как администратор** while logged out and **Выйти из режима администратора** while logged in. The logout tooltip explains that it disables session restoration on the next launch. Authentication behavior and the checkbox preference are unchanged.

Verification: `build-qt.ps1 -Package` passed (`smoke_qt` 1/1, `smoke_core: OK`). Authentication UI tests assert the action label and logout warning before/after persistent login, restoration and logout. Installer 0.6.21 passed isolated 0.6.20 update, installed startup with Qt removed from `PATH`, and uninstall; separate user data survived. Exact evidence, installer size and SHA-256 are recorded in `docs/releases/ForgeMirror-0.6.21.md`.

### Stage 176 — F10 window-decoration parity (implementation checkpoint)

F10 now toggles the native window frame and Qt drag handle, saves the choice to the workspace, and restores the prior normal/maximized/fullscreen presentation after the flag change. The keyboard-help dialog documents the shortcut.

Verification: `build-qt.ps1 -Package` passed (`smoke_qt` 1/1, `smoke_core: OK`). UI tests verify toggling both directions, persistence into a fresh window and drag-handle visibility. Installer 0.6.22 passed isolated 0.6.21 update, installed startup with Qt removed from `PATH`, and uninstall; separate user data survived. Exact evidence, installer size and SHA-256 are recorded in `docs/releases/ForgeMirror-0.6.22.md`.

### Stage 177 — Ctrl+F10 interface reset parity (implementation checkpoint)

Ctrl+F10 now resets Qt display, Pomodoro and 3D-view settings to their defaults, then restarts the client in the same isolated workspace. Reset is committed as one atomic `meta/ui.ini` replacement. The selected profile ID and `[profile]` trust/recent data remain intact, as do administrator credentials/session preference, cloud configuration, custom layout presets, model/music/background files and all task/profile data. Shortcut help documents the action.

Verification: `build-qt.ps1 -Package` and full installer lifecycle verification are recorded in `docs/releases/ForgeMirror-0.6.23.md`.

### Stage 178 — Project search and filter parity

The Projects page now imports the legacy `[projects] filter`, persists its search text in `[qt] projectFilter`, and restores it when returning to the page or reopening the Qt client. Search matches project name and description. **Сбросить фильтры** clears the query and overdue/pending-XP flags but leaves sort order unchanged. Regression coverage checks persistence, navigation, legacy import, reset behavior and accessible naming. Stage 178 lifecycle evidence is recorded in `docs/releases/ForgeMirror-0.6.24.md`.

### Stage 179 — administrator session process-restart verification

The regression suite launches the sibling `ForgeMirrorQt.exe` in a disposable workspace three separate times. Each process must restore administrator mode, retain the credential record and append an `Administrator session restored` event without a rejected-login event. The companion UI test verifies password acceptance/rejection, the remember-session checkbox, logout, repeated window restoration and password rotation. This closes the process-boundary test gap without reading or changing user credentials.

Verification and installer lifecycle results are recorded in `docs/releases/ForgeMirror-0.6.25.md`.

### Stage 180 — persistent quick-launch shortcut menu

The header now provides an always-available shortcut launcher whenever the module is enabled. Opening its menu refreshes process status once and displays green for running `.exe` names, red for stopped executables, and gray when status cannot be determined (including non-EXE targets). Launch actions use the saved shortcut path; the final menu action navigates to shortcut management. Tests cover all three states, accessible naming, management navigation, and module-toggle visibility.

Verification and installer lifecycle results are recorded in `docs/releases/ForgeMirror-0.6.26.md`.

### Stage 181 — global Pomodoro quick controls

The Qt header now keeps the current Pomodoro phase and remaining time visible on every page. Its menu exposes start/pause, next-interval and reset actions, and links to the full timer page. The quick next action follows the legacy manual-transition behavior; skipping a running focus interval does not award focus coins. Controls disappear when the Pomodoro module is disabled. Tests cover status refresh, pause/resume, manual phase transition, reset, navigation and module visibility.

Verification and installer lifecycle results are recorded in `docs/releases/ForgeMirror-0.6.27.md`.

### Stage 182 — navigation clock

The Qt navigation column now includes the stable interface's local analog clock and a seconds-resolution digital clock. Both use the existing Qt palette, update once per second, and expose accessible names; the timer is application-scoped and does not duplicate Pomodoro state. Regression coverage checks visibility, time formatting, timer cadence, accessible naming and rendered clock hands.

Verification and installer lifecycle results are recorded in `docs/releases/ForgeMirror-0.6.28.md`.

### Stage 183 — global cloud quick sync and status

The header now has a primary quick-sync action and a menu link to the Cloud page. Quick sync executes only the configured pull/push directions and, for a viewer with an unlocked profile, the existing wallet upload; it does not enable periodic synchronization. Administrators push before pull and the implementation keeps the Qt transactional push/pull recovery paths. A two-second status refresh shows disabled/unavailable cloud, drift or sync-file problems, or matching local/cloud files. The existing palette is unchanged.

Verification: `build-qt.ps1 -Package` passed (`smoke_qt` 1/1, `smoke_core: OK`). Service tests cover quick pull with periodic sync disabled and ensure periodic sync remains a no-op; UI tests cover the header action, status icon, menu navigation and disabled-cloud response. Packaged launch without Qt in `PATH` exited 0 and saved the smoke screenshot. Installer 0.6.29 lifecycle evidence is recorded in `docs/releases/ForgeMirror-0.6.29.md`.

### Stage 184 — remembered administrator login end-to-end verification

The process-boundary regression now performs the administrator login through the actual Qt dialog, checks **Не выходить после перезапуска**, and only then launches the packaged client as three independent processes against the disposable workspace. Each launch must restore the session, preserve the password and avoid a rejected-login event. This covers the complete path from the checkbox to process restart; production user credentials and workspace data are not read or modified.

Verification and installer lifecycle results are recorded in `docs/releases/ForgeMirror-0.6.30.md`.

### Stage 185 — detailed Qt layout geometry migration

The display settings now expose advanced controls for the legacy geometry values that were missing from Qt: window, frame, scrollbar and grab rounding; horizontal/vertical window and frame padding; and horizontal/vertical item spacing. Existing ImGui `[style]` values and read-only legacy layout presets import these metrics into the isolated Qt workspace. Custom Qt layout presets save and restore them. The metrics update Qt layout margins, control padding, list/table spacing, and scrollbar/slider rounding; the fixed Qt palette remains unchanged.

Verification: `build-qt.ps1 -Package` passed (`smoke_qt` 1/1, `smoke_core: OK`). Tests cover legacy style and preset import, custom preset round-trip, built-in preset preservation, persisted controls and applied stylesheet metrics. Administrator remembered-login coverage uses the real Qt dialog followed by three fresh processes; all restore the same session and password.

Verification and installer lifecycle results are recorded in `docs/releases/ForgeMirror-0.6.31.md`.

### Stage 186 — active trusted-session expiry and revocation

An unlocked 30/90-day profile session now revalidates the persisted trust expiry and trust entry on each access. If the time limit passes, Qt records `trust_expired`, clears the stale entry and locks the profile. If the trust entry changes externally before expiry, Qt closes its local session, records the invalidation and leaves the changed file untouched. Recovery journaling preserves the externally written state when audit persistence fails; the next access can retry expiry cleanup. Smoke coverage exercises external expiry, failed audit rollback, retry, external logout and relogin.

Verification: `build-qt.ps1 -Package` and installer lifecycle checks are recorded in `docs/releases/ForgeMirror-0.6.32.md`.

### Stage 187 — privacy-safe interface settings telemetry

Successful saves from the Qt display-settings dialog and the 3D-viewer settings page now append generic local application-log events. The events contain no selected paths, background names, or setting values. Cancelled or failed saves do not report success. Smoke tests save settings through both real Qt pages and verify their persisted events; the interface-settings test closes its reader before later atomic log rewrites.

Verification: `build-qt.ps1 -Package` passed (`smoke_qt` 1/1, `smoke_core: OK`). The isolated installer lifecycle updated 0.6.32 to 0.6.33, launched with Qt removed from `PATH`, and uninstalled while preserving a separate user-data marker and the stable ImGui uninstall record. Evidence and SHA-256 are recorded in `docs/releases/ForgeMirror-0.6.33.md`.

### Stage 188 — task creation-month report dimension

The Qt Statistics report can now group the selected task cohort by its local creation month, retaining a separate **Без даты** bucket for legacy records without a timestamp. It uses the existing current/previous-period metrics, row search, task drill-down, table CSV export and persisted grouping selector. Existing grouping indexes remain unchanged; the new grouping is appended at index 7. The fixed Qt palette is unchanged.

Verification: `build-qt.ps1` passed (`smoke_qt` 1/1, `smoke_core: OK`). The report regression covers current and previous creation-month cohorts, counts, drill-down, CSV and persistence. The versioned installer and lifecycle verification for 0.6.34 are recorded in `docs/releases/ForgeMirror-0.6.34.md`.

### Stage 189 — reject stale external profile writes

The file-backed profile store now remembers the bytes selected/read for the active profile and checks them immediately before a save. A profile replaced by another client is left untouched and the stale save fails; callers must reload before retrying. Profile writes now use unique sibling temporary files and atomic replace without deleting the destination first, eliminating the prior Windows delete-then-rename loss window. Smoke coverage exercises a replaced profile and confirms byte-exact preservation after rejection. This is a per-file optimistic guard; multi-file external workspace transactions remain unsupported.

Verification: `build-qt.ps1` passed (`smoke_qt` 1/1, `smoke_core: OK`). The versioned installer and lifecycle verification for 0.6.35 are recorded in `docs/releases/ForgeMirror-0.6.35.md`.

### Stage 190 — stale-target-safe cloud pull recovery

Cloud-pull transaction journals now record both the pre-image and the expected post-image hash and existence state for every changed or removed file. Recovery validates every target against those two known states before restoring anything. If a file has a third state, recovery stops with the journal and backup intact, so it cannot overwrite an edit made after an interrupted pull or partially roll back the other targets. After the external conflict is resolved, recovery can be retried. Legacy version-1 pull journals, which lack post-image hashes, are completed automatically only when every target still matches its pre-image; ambiguous old journals fail closed for manual inspection. The built-in `--version` and `--help` options now return before any workspace is created or the main window opens.

Verification: `smoke_qt` covers a multi-file recovery where one target has an external edit, proves no target is changed on conflict, and retries successfully after the edited target is restored to the expected pull image. It also verifies conservative version-1 recovery and launches separate client processes to check that `--version`/`--help` exit without creating a default workspace. Broader non-cloud multi-file transactions remain a separate parity gap.

### Stage 191 — safe restoration of the Pomodoro startup page

Restoring the saved Pomodoro page during `QtWindow` construction could crash Qt Widgets on Windows before the main window was shown. Qt now builds the initial window on the profile page and restores Pomodoro through the event queue after display. This keeps the saved page and persistent administrator session intact while avoiding the crash. Regression coverage starts a fresh Qt window with a remembered administrator session and Pomodoro selected, checks that the administrator controls and page become visible, and leaves the event loop running through delayed startup callbacks.

### Stage 192 — preserve external cloud edits during interrupted push recovery

The cloud-push transaction test now interrupts a real multi-file push after its first write, changes another target as an external writer, and verifies recovery rejects the entire rollback without changing any cloud bytes. After restoring the external file to the transaction's expected post-image, recovery must roll the full cloud tree back to its exact pre-push inventory and remove the journal. `build-qt.ps1` passed, including `smoke_qt` and `smoke_core`. This is test coverage for the existing recovery behavior, not a new application feature or installer release.

### Stage 193 — preserve changed files before local transaction rollback

When startup finds an interrupted multi-file profile/task/catalog transaction, it copies each changed, newly created, or unexpectedly missing target state to `meta/updates/qt-xp-recovery-<timestamp>-<sequence>/` before restoring the journal pre-images. A manifest records which files were copied and which were missing. The recovery folder is outside the fixed cloud file allowlist. The UI status and administrator application log identify the folder; successful rollback behavior remains unchanged. Immediate checked rollback after a failed in-process operation does not create these extra copies.

Verification: smoke simulates a partially written task edit and verifies byte-exact preservation of the in-flight versions alongside rollback. A separate constructor/startup case checks that the preservation path survives both workspace initialization passes and is reported in the application log. `build-qt.ps1` passes `smoke_qt` and `smoke_core`.

Verification and installer lifecycle results are recorded in `docs/releases/ForgeMirror-0.6.37.md`.

### Stage 194 — large text accessibility scale

The interface text-scale control now includes 150%, 175% and 200% options. Existing 90–125% choices, the fixed migration palette, saved Qt settings format and legacy preset imports remain compatible. Layout presets can store and restore the larger scales. At 150% and above, the client opens maximized, scales fixed control and navigation dimensions, and provides vertical and horizontal scrolling for page content; table rows scale with the text. The smoke test verifies 200% settings/preset persistence, startup layout state and the scroll container. This adds large-text support but does not replace a full screen-reader audit.

Verification: `build-qt.ps1 -Package` passed (`smoke_qt` 1/1, `smoke_core: OK`). Packaged startup screenshots were captured at 100% and 200% with Qt removed from `PATH`; the 200% view showed enlarged controls and a scrollable content area. The versioned installer lifecycle is recorded in `docs/releases/ForgeMirror-0.6.39.md`.

### Stage 195 — screen-reader names in dialogs

The Qt accessibility smoke now inspects every visible interactive control in the main window, administrator login, display settings (including expanded geometry), and nested background settings through `QAccessible`. This exposed two unnamed combos that sat inside composite form rows: the built-in layout selector and background tile scale. Both now have explicit accessible names and descriptions. The administrator remember-session checkbox is also checked through the accessibility interface, including its checked state, before the existing save/restart persistence test continues. The smoke now audits visible interactive controls in each modal it exercises; dialogs not reached by automated workflows and hands-on screen-reader behavior still need review.

Verification: `build-qt.ps1` passed (`smoke_qt` 1/1, `smoke_core: OK`). Windows installer lifecycle evidence is recorded in `docs/releases/ForgeMirror-0.6.40.md`.

### Stage 196 — core task-completion outcome events

CompleteTaskWithXp now reports generic validation, failure/rollback, and commit outcomes through an optional AppContext event sink. The sink is isolated from domain results: a throwing observer cannot change a successful XP transaction. Qt connects the task-completion dialog's existing logger to this core path, removing duplicate UI-side outcome logging. Default and ImGui AppContext instances remain no-op, preserving their behavior. Tests verify outcome levels/messages, absence of task/profile identifiers and names, and exception isolation. Broader core event coverage and live external writers during multi-file transactions remain open.

### Stage 197 — broad modal accessibility audit

The smoke test now observes modal windows as they open and audits visible interactive controls through QAccessible across the existing workflows. This covered 24 modal instances in the complete smoke run and exposed missing names/descriptions on cloud and storage comparison tables, achievement records and duration, skill-profession bindings, profile activity/task history, file export controls and XP rule presets/history. Each application-owned control now has a descriptive accessible name and context; Qt's non-interactive table corner control is excluded from the audit. This is static accessibility metadata coverage, not an NVDA/JAWS interaction certification.

Verification and installer lifecycle results are recorded in docs/releases/ForgeMirror-0.6.42.md.


### Stage 198 — semantic chart data for assistive technology

Accessible descriptions now expose the report chart selected-period status totals and pair each 12-month completion count with its month. Profile analytics describe category scores, the top-six total-XP ranking and the selected radar axes with fractional level progress; changing the axis count refreshes the description. The application-log histogram announces its chronological bin values, timestamp range, filter independence and how missing or indistinguishable timestamps are distributed. Repeated chart refreshes replace their descriptions rather than accumulating duplicate text.

Smoke verifies these values through QAccessible. This improves semantic access to the Qt-painted charts but does not certify behavior with an external screen reader.

Verification and installer lifecycle results are recorded in `docs/releases/ForgeMirror-0.6.43.md`.


### Stage 199 — transaction outcomes from core services

Profile editing, administrator password reset and normal password change now emit generic commit or rollback events from their journaled core wrappers. Rules reapplication, direct skill-XP grants, and single/bulk task deletion do the same; the Qt dialogs connect these optional observers to the existing bounded application log and admin Audit source. Core callbacks are exception-isolated, and messages omit profile/task IDs, names, field contents and password values. UI-side duplicates were removed so each service outcome is recorded once.

Smoke verifies committed and rolled-back profile/password/rules/direct-XP outcomes and keeps profile identifiers and entered passwords out of the event messages. Broader services and non-Qt core operations remain outside the telemetry sink.

Verification and installer lifecycle results are recorded in `docs/releases/ForgeMirror-0.6.44.md`.

### Stage 200 — stale task snapshot protection

Task saves now compare the caller's expected task snapshot with the last known saved snapshot and the current task file bytes. If another process changes `meta/tasks.json`, the service rereads the strict JSON and rejects a stale single, bulk, status, deletion, XP-finalize, or project-detach mutation; it restores the caller's in-memory edit and leaves the changed task file intact. A small per-process cache avoids reparsing unchanged task JSON on each normal save. The project-delete path rolls back its project edit without writing stale tasks. Smoke covers stale single/bulk/delete/project-delete rejection, byte-exact preservation, and a successful retry after explicit reload.

This is an optimistic pre-write guard, not a cross-process lock: a writer that changes the task file in the interval after comparison and before atomic replacement can still race. Coordinated writers and multi-file external transactions remain outside the guarantee. Stable ImGui behavior and `develop` remain unchanged.

Verification and installer lifecycle results are recorded in `docs/releases/ForgeMirror-0.6.46.md`.

### Stage 201 — preserve changed files on immediate transaction rollback

Journaled multi-file operations now use the same changed-file preservation when an in-process failure triggers rollback as startup recovery already used. Before restoring journal pre-images, recovery copies changed, newly created, or unexpectedly missing targets to `meta/updates/qt-xp-recovery-<timestamp>-<sequence>/`, records the exact targets in a manifest, and includes the path in the UI failure message. This covers task completion/edit/deletion, project and catalog operations, profile mutations, rules and session audit transactions. It preserves in-flight bytes when a write fails or an external edit is observed before rollback; it does not prevent live external writes from racing a successful operation.

Smoke forces a divergent catalog file during immediate journal recovery and verifies byte-exact rollback, a separately preserved external version, manifest entry, and user-visible recovery path. `build-qt.ps1` passed (`smoke_qt` 1/1, `smoke_core: OK`). Release and installer lifecycle evidence are recorded in `docs/releases/ForgeMirror-0.6.46.md`.

### Stage 202 — core telemetry for journaled task status changes

`UpdateTaskStatusWithRecovery` now accepts an optional privacy-safe event sink and emits one generic committed or failed/rolled-back outcome after the journal operation finishes. Observer exceptions are isolated from the task result; default and ImGui call paths remain unchanged. Qt connects the sink to its bounded local application log and no longer writes a duplicate status event itself. Smoke verifies both outcome levels/messages and that a throwing observer cannot alter a successful status update.

Verification and installer lifecycle results are recorded in `docs/releases/ForgeMirror-0.6.47.md`.

### Stage 203 — privacy-safe core telemetry for profile creation

`AppCreateProfile` now accepts an optional event sink and reports rejected, failed, or committed outcomes using generic messages. Neither the profile name nor its returned ID, login, or one-time password enter the event. Qt connects its existing profile mutation logger; legacy callers keep the no-op default. Smoke covers validation failure, success, secret exclusion, and a throwing observer.

Verification and installer lifecycle results are recorded in `docs/releases/ForgeMirror-0.6.48.md`.

### Stage 204 — core telemetry for profile archive and restore

The journaled archive/restore service now emits one generic outcome after commit or rollback through an optional exception-isolated event sink. Qt connects it to the profile core-event log and removes no extra UI-side event. Tests exercise failed archive, successful archive and restore outcomes without recording profile identifiers.

Verification and installer lifecycle results are recorded in `docs/releases/ForgeMirror-0.6.49.md`.

### Stage 205 — core telemetry for task metadata CRUD

Journaled task creation, metadata editing, plain deletion and awarded-record deletion with XP preserved now emit one generic outcome through an optional exception-isolated event sink. Qt passes its bounded task-event logger into the service and removes duplicate UI-side success/failure events. Task IDs, titles, descriptions and participant data are never logged. Smoke covers committed and rolled-back service outcomes.

Verification and installer lifecycle results are recorded in `docs/releases/ForgeMirror-0.6.50.md`.

### Stage 206 — serialize cooperating task writers

`AppSaveTasks` and `AppSaveTasksIfUnchanged` now share a short OS-level lock on `meta/tasks.json.lock`. The optimistic snapshot comparison and atomic replacement stay within one lock lifetime, so another process using the same save APIs cannot enter between check and write. Windows uses an exclusive non-reparse file handle; Unix-like builds use nonblocking `flock` on a no-follow regular file. A contending writer fails without changing task bytes, and the UI receives a retry message. The stable ImGui implementation and data formats are unchanged; writes from binaries or tools that bypass these shared APIs, and cross-file external transactions, remain outside the lock guarantee.

Smoke holds the lock from a second OS handle, confirms both conditional and unconditional saves refuse without changing the task file, then releases it and verifies a retry succeeds. The task lock file is recognized as an internal workspace file, not a stray item.

Verification and installer lifecycle results are recorded in `docs/releases/ForgeMirror-0.6.51.md`.

### Stage 207 — serialize cooperating profile writers

`FileStorage` uses one OS-level workspace lock around profile save compare-and-replace, achievement persistence, archive/restore moves, deletion and profile-ID normalization. This closes the compare/write race between current ForgeMirror clients built on the shared core; stale snapshots still fail without replacing newer profile bytes. The persistent `meta/profile-write.lock` sidecar is internal metadata and is omitted from seed workspace copies. Clients or tools that bypass `FileStorage` and external multi-file transactions remain outside this guarantee.

`smoke_core` holds the lock through a second OS handle, verifies that a contending profile save is refused without changing bytes, releases the lock and verifies a retry succeeds. It also verifies the lock sidecar is not reported as a stray file. The stable ImGui executable was rebuilt against the shared core; its UI implementation is unchanged.

Versioned installer lifecycle results are recorded in `docs/releases/ForgeMirror-0.6.52.md`.

### Stage 208 — coordinate Qt achievement writes with profile persistence

`AppProfileStorageWriteLock` is now a shared core primitive used by `FileStorage` and Qt achievement grant/edit/revoke read-modify-write operations. Qt acquires it before profile and sidecar reads and holds it through atomic replacement, preventing current cooperating clients from racing over profile-derived achievement state. Failure to acquire returns a retry message without changing achievement bytes. Older binaries and external tools that bypass this lock remain outside the guarantee.

`smoke_qt` verifies contention refusal leaves achievement bytes unchanged and retry succeeds after release. The reusable lock source is included in CLI, stable ImGui, Qt and core test targets. Version 0.6.53 installer lifecycle verification is recorded in `docs/releases/ForgeMirror-0.6.53.md`.

### Stage 209 — verify the initial workspace import snapshot

The first-run stable-workspace import now copies regular files and directories into a unique staging sibling, skips symbolic links and Windows reparse points, and compares SHA-256 snapshots of the source before/after copying and the staged result. If the source changes during copying, the Qt workspace is not published; only this attempt's staging directory is removed. Existing destinations and overlapping source/destination roots are rejected. Source files are read-only throughout.

`smoke_qt` verifies a complete nested copy, source preservation, existing-destination refusal and overlapping-root refusal. Version 0.6.54 installer lifecycle results are recorded in `docs/releases/ForgeMirror-0.6.54.md`.

### Stage 210 — profile credential copy and generated password reset

Profile creation and administrator password reset expose explicit copy actions for login and password. Password copying stays disabled until the user reveals the generated credential; reset requires a separate confirmation and leaves the credential dialog open after the transactional password update so it can be saved. Authentication secrets are not added to logs. The guided pipeline transition selector also has an explicit accessible name and description. `smoke_qt` exercises both copy flows, masked state, confirmation, transactional rollback, the subsequent personal password change using the generated reset credential, and visible-control accessibility names. The 0.6.55 installer verification is recorded in `docs/releases/ForgeMirror-0.6.55.md`.

### Stage 211 — window menu parity

The overflow menu now contains an **Окно** submenu with checkable **Во весь экран (F11)** and **Без рамки (F10)** actions matching the stable ImGui menu. The F10/F11 shortcuts trigger those same actions, so menu checks, persisted workspace settings and live window state stay synchronized. Smoke coverage exercises both menu and shortcut routes, persistence across a window reconstruction, the native drag handle, and the fullscreen flag. The palette and display styling are unchanged.

### Stage 212 — cloud manifest update timestamp

The Cloud page now displays the manifest's **Данные обновлены** timestamp using the same local `yyyy-MM-dd HH:mm` representation as the stable ImGui menu. A missing or invalid timestamp is shown as an em dash. The cloud-page smoke verifies the row against a fixed manifest timestamp; no cloud files or synchronization behavior are changed.

### Stage 213 — serialize cooperating workspace mutations

The task-only and profile-only lock files are replaced by one reentrant OS-level `meta/workspace-write.lock`. Shared core save paths and Qt catalog/session read-modify-write operations use it. Recovery-journal operations hold the lock from pre-image snapshot creation until commit or checked rollback; nested ordinary save helpers can re-enter it on the owning thread. This prevents cooperating current builds from interleaving a multi-file transaction. Older binaries and external tools that ignore the new lock remain outside the guarantee; their legacy sidecar files are retained as allowed internal metadata.

`smoke_qt` verifies same-thread reentrancy, cross-thread exclusion, nested saves while a recovery journal is active, and release after commit; the achievement contention test now uses a competing thread. `smoke_core` verifies core task/profile writes fail without changing bytes while the workspace lock is held and succeed after release. Both `smoke_qt` and `smoke_core` pass. The dark palette, Qt workflows and stable ImGui interface are unchanged.

Installer lifecycle and package verification are recorded in `docs/releases/ForgeMirror-0.6.58.md`.

### Stage 214 — profile creation history parity

Creating a profile through the Qt manager now appends the same `create` event to `meta/profile-audit.log` as the stable ImGui flow. The audit contains the generated login, never the generated password. If the profile is already created but the best-effort audit append fails, the manager says so while leaving the new credentials available to save.

`smoke_qt` drives the real manager twice: an injected audit-write failure confirms the profile remains created and the missing history entry is reported; a successful creation verifies the `create` row and confirms the password is absent from audit bytes. Installer lifecycle and package verification are recorded in `docs/releases/ForgeMirror-0.6.59.md`.

### Stage 215 — profile deletion outcome telemetry

Permanent deletion of an empty archived profile now records one privacy-safe core event after the recovery transaction commits or its rollback/recovery result is known. A failed rollback is marked as recovery pending. The event contains neither profile ID nor name, and an event-sink exception cannot change the deletion result.

`smoke_qt` injects a real storage deletion failure through the profile manager, verifies rollback leaves the profile intact and records a warning, then retries and verifies the committed event and deletion even when the event observer throws. It also checks that neither event includes the disposable profile ID or name. Installer lifecycle and package verification are recorded in `docs/releases/ForgeMirror-0.6.60.md`.

### Stage 216 — refresh profile IDs for already-open storage clients

`FileStorage::create_profile` now acquires the shared workspace-write lock before allocating an ID, recalculates the next numeric ID while holding it, and retains the lock through the nested profile save. This prevents a long-lived client with a stale `nextId_` snapshot from replacing a profile created by another cooperating client. Lock contention still fails without writing. The stable ImGui UI and workspace format are unchanged.

`smoke_core` opens two storage clients before either write, then creates profiles sequentially and verifies distinct IDs and both persisted names. The previous implementation failed this regression by overwriting the first file. Build and installer lifecycle results are recorded in `docs/releases/ForgeMirror-0.6.61.md`.

### Stage 217 — serialize cloud workspace mutations

Manual Qt pull/push and recovery, preview, cloud file/catalog conflict transfers, storage-conflict resolution, release downloads, and shared core cloud-sync mutators now hold the reentrant workspace-write lock while reading or changing local workspace state. Pulling into Qt's isolated staging copy uses an explicitly named staging API, while the publishing transaction retains the lock on the live workspace through backup, journal, commit, or rollback. If another cooperating writer holds the lock, the operation exits before changing workspace/cloud data.

`smoke_qt` and `smoke_core` hold the OS lock from a second handle and verify cloud writes, pulls, conflict resolution, config saves, and journal recovery refuse without partial mutations; existing failure-injection and recovery cases still exercise the normal path. Installer and package lifecycle results are recorded in `docs/releases/ForgeMirror-0.6.62.md`.

### Stage 218 — keep profile analytics controls clear of charts

The radar-axis toolbar is top-aligned and the custom chart reserves its live layout height plus spacing before painting. This keeps the control clear of the radar heading when Windows style metrics or Qt text scaling change, while preserving the existing palette and chart geometry. `smoke_qt` verifies the control stays inside the reserved toolbar area; the packaged-window screenshot confirms the radar heading and control no longer overlap.

Installer lifecycle and package verification are recorded in `docs/releases/ForgeMirror-0.6.63.md`.

### Stage 219 — serialize legacy storage-conflict confirmation

The ImGui compatibility client now applies a saved cloud `storage.json` conflict through the shared core operation. It acquires the workspace-write lock, accepts only a validated conflict snapshot from `meta/updates`, saves and verifies a local pre-image, stages and validates the cloud file, then atomically replaces the vault. A rejected or contended operation leaves the active vault unchanged. The preserved baseline branch and `develop` are untouched.

`smoke_core` covers invalid/out-of-directory snapshots, lock contention, successful replacement, and exact preservation of the local backup. The Qt smoke suite and the ImGui compatibility target build also pass. Release package and installer lifecycle verification are recorded in `docs/releases/ForgeMirror-0.6.64.md`.

### Stage 220 — make bulk task edits one recovery transaction

All six administrator multi-select task updates now run inside the shared recovery journal covering `tasks.json`, `task-audit.log`, and the last-good task snapshot. If a task save or any audit append fails after the task file changed, the service restores the exact pre-operation files and in-memory task/audit state; changed in-flight bytes are retained by the standard recovery path. The mutation outcome is emitted once from the core service through the privacy-safe event sink.

`smoke_qt` forces task-audit failure after a bulk priority change and verifies byte-exact file rollback, restored in-memory state, no pending journal, and one warning event. It also verifies a successful bulk update remains committed when the observer throws. The end-to-end bulk editor continues to cover status, priority, project, pipeline, deadline and assignee changes. Release package and installer lifecycle verification are recorded in `docs/releases/ForgeMirror-0.6.65.md`.

### Stage 221 — preserve concurrent log entries and confirm clearing

Every application-log append now takes the shared workspace-write lock, reads the latest on-disk history and merges only entries still pending in this process before atomically replacing the bounded 200-entry JSON file. Two open Qt windows can no longer silently replace each other's newer log entries with stale in-memory snapshots. The Logs page now matches its accessible description: clearing opens a warning dialog, defaults to Cancel, and leaves both memory and disk unchanged on cancellation.

`smoke_qt` opens two windows over the same test workspace, appends from each and verifies that both entries survive while the retention limit still holds. It also checks byte-exact preservation after cancelling clear and successful persistence after explicit confirmation. The CTest timeout was raised to 120 seconds because the full UI smoke is near the former 30-second limit on this host. Release package and installer lifecycle verification are recorded in `docs/releases/ForgeMirror-0.6.66.md`.

### Stage 222 — preserve concurrent banner and vault changes

Banner add/update/delete now reload current metadata under the shared workspace lock. New phrases merge with intervening additions; stale edit/delete actions fail with a refresh message instead of replacing another writer's change. Invalid banner documents, symbolic-link targets and temporary-file links are refused. Vault settings update only configuration fields from the latest saved vault, retaining newer balance and journal entries. Privacy-safe core mutation events cover success and failure outcomes without recording phrase text, currency or amounts.

`smoke_core` checks stale edit/delete rejection, merged additions, malformed-file preservation, symlink refusal where supported, atomic write failure and vault balance/journal preservation. `smoke_qt` drives both real editors with a newer external write while each dialog is open; `smoke_qt` and `smoke_core` pass. Release package and isolated installer lifecycle evidence are recorded in `docs/releases/ForgeMirror-0.6.67.md`.

### Stage 223 — reject stale pipeline mutations

Pipeline add, edit, delete and reorder now acquire the shared workspace lock before loading the latest normalized pipeline and retain it through the recovery-backed write. If the editor's snapshot no longer matches disk, the service refreshes the caller's list and refuses the mutation, preventing an older cooperating client from silently dropping a concurrent stage edit or addition. Lock contention exits before changing in-memory or persisted pipeline data.

`smoke_core` verifies stale edits and deletes are rejected, external changes remain persisted, and the caller's snapshot refreshes after conflict. Existing save-failure rollback, link cleanup and reordering cases still pass. `smoke_qt` and the core suite pass. Release package and isolated installer lifecycle evidence are recorded in `docs/releases/ForgeMirror-0.6.68.md`.

### Stage 224 — guard full-list pipeline editor saves

The Qt pipeline editor now saves through the shared compare-and-write service instead of directly replacing the complete pipeline file. It captures the full list when the dialog opens, checks both that snapshot and the live workspace against disk while holding the shared write lock, and updates the live list on conflict without applying the draft. A stale editor disables Save and asks the user to reopen it, avoiding repeated retries against the same obsolete snapshot.

`smoke_core` verifies stale full-list candidates are rejected, refreshed, and can be saved after reload without losing concurrent fields. `smoke_qt` makes an external pipeline edit while the real editor is open, verifies the edit survives and Save is disabled, then reopens the editor and commits successfully. Release package and isolated installer lifecycle evidence are recorded in `docs/releases/ForgeMirror-0.6.69.md`.

### Stage 225 — preserve the legacy F6 destination

The action-level source audit found that the Qt shortcut F6 opened the new Audit page, while the stable ImGui client binds F6 to Logs. Qt now sends F6 to Logs, shows the shortcut on that navigation item, and documents the same destination in shortcut help. Audit remains a separate Qt page without taking over the legacy key.

`smoke_qt` verifies the F6 page mapping, navigation tooltip, absence of the shortcut from Audit, and the help-table label. `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.70` passed (`smoke_qt` 1/1, `smoke_core: OK`). Packaged startup with Qt removed from `PATH` exited 0 and saved `build-qt/runtime-0.6.70.png`. Release and isolated installer lifecycle verification are recorded in `docs/releases/ForgeMirror-0.6.70.md`.

### Stage 226 — preserve the legacy F5 statistics destination

The stable **Статистика [F5]** page is a profile statistics dashboard, not a task-report page. Qt now maps F5 to **Статистика профилей** (the migrated profile dashboard); the newer **Статистика** reports page remains independently accessible. The navigation hint and shortcut-help table name the profile dashboard as F5's destination.

`smoke_qt` verifies the F5 page mapping, navigation tooltip, and shortcut-help destination alongside the F6 Logs regression. `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.71` passed (`smoke_qt` 1/1, `smoke_core: OK`). Packaged startup with Qt removed from `PATH` exited 0 and saved `build-qt/runtime-0.6.71.png`. Release package and isolated installer lifecycle evidence are recorded in `docs/releases/ForgeMirror-0.6.71.md`.

### Stage 227 — restore statistics dashboard bars

Legacy profile statistics show mini-bars beside category averages and rank shares. Qt now renders those same metrics as compact accessible progress bars while retaining their numeric values in each table cell. The existing purple accent remains unchanged. `smoke_qt` checks the bar ranges, values, accessible names and numeric table values in both views.

Verification: `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.72-release` passed; `smoke_qt` and `smoke_core` results plus package startup are recorded with the versioned installer evidence in `docs/releases/ForgeMirror-0.6.72.md`.

### Stage 228 — restore profile-manager filters and refresh

The Qt profile manager now matches the legacy list's search fields (name, ID, login and profession), all/active/archive filter, profession filter (including profiles without one), ID/name sort, and explicit list refresh. The table shows each profile's profession. Refresh uses read-only profile snapshots for active and archived records, so filtering does not switch the workspace's selected storage profile. `smoke_qt` verifies each filter, both sort modes, manual refresh after an external profile addition, and preservation of the selected profile.

The change adds the missing profile-management actions; it does not close the remaining migration work. Estimated functional migration remains about 90% pending the broader action-level audit.

Verification: release and isolated installer lifecycle evidence are recorded in `docs/releases/ForgeMirror-0.6.73.md`.

### Stage 229 — restore Pomodoro sound discovery and refresh

The Qt sound selector now has the legacy **Обновить список звуков** action, keeps the current selections when refreshed, and searches the installed product's `data/music` assets if the isolated workspace has no local `music` folder. The Qt packager now includes only the product music assets in that location. Existing workspace sounds still take precedence, and the fixed interface palette is unchanged. `smoke_qt` covers live refresh, selection preservation, safe audio filtering, and bundled-asset discovery.

Verification: `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.74-release` and isolated installer lifecycle evidence are recorded in `docs/releases/ForgeMirror-0.6.74.md`.
