# Qt Widgets migration

## Branches

- `codex/pre-qt-2026-08-28`: exact stable ImGui snapshot, commit `7306152`, version 0.5.54.
- `codex/qt-gui`: incremental migration. `develop` and the ImGui implementation remain unchanged.

This is **stage 40**, not a feature-complete replacement for ImGui. Existing storage formats and domain services are reused. The Qt-only `AppTaskCompletionService` adds transactional cross-file recovery without changing the stable ImGui implementation. The Qt client now has user-facing version `0.6.5` and a separately verified per-user installer; the preserved ImGui baseline remains version 0.5.54.

## Build and run

Design direction for subsequent UI work: [user-supplied interface references](../docs/design/INTERFACE_REFERENCES.md).
These guide composition and hierarchy; the existing dark/purple palette is unchanged.

```powershell
.\build-qt.ps1 -Package
.\package-qt\ForgeMirrorQt.exe
.\installer\build-qt-installer.ps1
```

The portable directory is a QA output, not the release deliverable. The current installer is `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.5.exe`. Version `0.6.5` comes only from the root `VERSION` file and is propagated into the application, Windows EXE metadata, installer metadata and artifact name. Full install/update/uninstall evidence and SHA-256 are recorded in `docs/releases/ForgeMirror-0.6.5.md`.

Requires MSVC 2022, CMake and Qt 6.8+ Widgets/Test. Override the default installed Qt path using `-QtRoot`.

On first launch, Qt offers to **copy** the stable workspace to its own local application data directory (`Pharos/ForgeMirrorQt/workspace`). Cancel aborts startup; No starts an empty workspace. Existing Qt workspaces are never reimported automatically. A staged import prevents a partial copy from being treated as complete. Reparse/symlink entries are skipped. Do not edit the source while importing.

Qt never calls the cloud sync service. No Qt changes are written back to the original workspace. `FORGEMIRROR_STORAGE_DIR` identifies the source for the initial import, not Qt's output directory. A failed import may leave an `import-<uuid>` staging directory for inspection.

`--storage-dir <path>` opens an explicit disposable development workspace. Production directory paths, parents and children are rejected. Do not deliberately point it at other live storage folders. A lock prevents multiple Qt clients from editing the same Qt workspace.

Admin mutations require the existing admin password from the copied settings (the existing core's initial default for empty storage is `admin123`). Admin sessions are not persisted. Profile viewing remains available without a personal login session. Profile management is administrator-only; normal password changes require the current profile password. Personal unlock may remain session-only or trust this local Qt workspace for 30/90 days.

## Coverage

| Area | Qt stage 40 | Remaining |
| --- | --- | --- |
| Profiles | Selector, compact level/XP/task metrics, skills, transactional direct skill/global XP grant, admin creation/archive/restore, guarded permanent deletion of empty archived profiles, rename with stable ID, profession/spirit/block editing, password reset/change, session or 30/90-day local trust, achievement viewing/granting/editing/revocation and local icons; wallet balance and personal evil-spirit removal | Other wallet operations and richer activity history |
| Tasks | List, search, status filter, details and awarded XP, restart-safe admin creation/status/edit/completion; confirmed deletion before XP; guarded transactional deletion of current Qt-v2 awards with profile rollback | Bulk operations, reminders, legacy/stale awarded-task cleanup review |
| Projects | Admin list, creation, editing and confirmed deletion with task detachment; stable IDs and current names in linked tasks | Embedded project focus |
| Skills | Catalog viewing/search, admin creation/editing and guarded deletion of unused records with checked persistence | Merge and dedicated profession filters |
| Pipeline | Stages, details, admin creation/editing, checked deletion of unused stages, atomic up/down reordering; current names in task rows and guided next-step transitions | Visual branch map |
| Professions | Admin list, creation/editing and guarded deletion; assignment through profile manager and skill editor | Merge and archived-profile reassignment workflow |
| Reports | Admin project/employee metrics with resolved profile names, search and atomic UTF-8 CSV export | Date ranges, charts and richer drill-down |
| Audit | Admin task and profile-access audit view | Other application logs |
| Rules | Administrator F4 summary, checked editor and confirmed transactional level recalculation for active and archived profiles while preserving total XP | Rule presets and change history |
| Display | Local 90/100/110/125% text scale and compact-table density; fixed migration palette | Additional accessibility options |
| Other | Separate workspace, refresh, contextual keyboard shortcuts, local program shortcuts with add/open/reorder/delete, F1–F6 navigation, shortcut help, Pomodoro timer/settings/sounds, guarded rewards and administrator vault settings/log | Cloud, 3D and remaining settings |

### Local program shortcuts (stage 40)

The **Ярлыки** page ports the stable local launcher for every user. It lists the stored label, path and current availability, adds an existing file through a compact picker dialog, opens the selected path through `QDesktopServices`, reorders entries and removes only the shortcut record after confirmation. Deleting a shortcut never deletes its target file, and no path is interpolated into a shell command.

Shortcut JSON now uses the shared checked atomic replacement path on Windows. A failed write keeps both the in-memory order and the previous file bytes. Tests cover add/reload/reorder, injected persistence failure with rollback, the real non-administrator page and dialog, target preservation after deletion, and native layouts without clipped controls.

### Profile management

On the Profile page, an administrator can open **Управление профилями**. The searchable list includes an optional archive view. Creation uses the existing service and generates a login/password; credentials are masked until explicitly revealed and are not copied to the clipboard or written to logs. Store them before closing the manager.

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

Before writes, `meta/qt-xp-transaction` receives original profile bytes, tasks, task audit and the last-good task backup. Failed writes restore these files and in-memory task/audit data. If rollback cannot finish, the journal is retained and further Qt mutations are blocked. Pending transactions are recovered before loading the workspace on startup or refresh; malformed journals fail closed and require inspection. A committed journal is renamed before cleanup so a process interrupted during cleanup does not undo a successful completion. This is local recovery, not a guarantee against disk failure or concurrent external edits: do not open the Qt workspace in ImGui or edit its files during an operation.

The full-profile journal is only for failed/interrupted transactions. Successful tasks retain the existing XP rollback snapshot for future task-deletion migration. The stable ImGui transaction code remains untouched.

## Verification

`build-qt.ps1 -Package` builds the Qt client and executes `smoke_qt` plus the existing `smoke_core`. Qt tests use temporary workspaces and exercise loading, search, status filtering, HTML escaping, administrator login/logout, project/task forms, status persistence, keyboard navigation and byte-preserving profile viewing. XP tests cover form cancellation/validation/completion, exact modifiers and rounding, zero XP, blocked profiles, duplicate awards, failed second-profile/task/audit writes, byte-exact rollback, simulated restart recovery and rejection of unsafe recovery paths.

Profile tests additionally exercise creation, edit-save failure, preservation of XP/wallet/credentials, archive cancellation/restore, password confirmation and current-password validation, and the administrator entry point.

For visual QA, run `smoke_qt.exe -platform windows` with Qt `bin` on PATH and `FORGEMIRROR_QT_TEST_ARTIFACTS` set to a disposable output directory. It captures the real XP dialog at normal and minimum sizes. The offscreen platform is appropriate for interaction tests but may render missing font glyphs on Windows.

For a packaged startup check (no installed Qt on PATH):

```powershell
.\package-qt\ForgeMirrorQt.exe --storage-dir Z:\CPP\ForgeMirror\build-qt\runtime-test --smoke-test --screenshot Z:\CPP\ForgeMirror\build-qt\qt-window.png
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

A complete candidate collection is saved once through AppSavePipelineData and its atomic primary-file replacement. Memory is updated only after success. Pending task/XP recovery blocks saving. Task and profile files are not rewritten when editing definitions; task rows resolve the current stage title by ID. Guided progression along nextIds is still pending, as are deletion and reordering.

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

The overflow menu exposes local Qt display settings to every user. Text scale can be set to 90, 100, 110 or 125 percent, and table rows can switch between the normal 28px and compact 24px density. Both changes apply immediately. The migration palette remains fixed, following the recorded design decision to preserve existing colors.

Settings use a dedicated `[qt]` section in `meta/ui.ini`. The writer preserves the BOM, unrelated sections and unknown lines, rejects symlinked metadata paths and replaces the file through `QSaveFile` without direct-write fallback. This coexists with trusted-profile and Pomodoro settings rather than overwriting them. Simultaneous external writers remain unsupported.

Tests cover dialog persistence, reload, live font application, compact row application, preservation of unrelated bytes and a real Windows sharing violation. The 125% dialog and compact main table were inspected natively without clipping.

### Keyboard shortcuts (stage 23)

The Qt window now provides shortcuts only for actions already implemented safely in the current page: Ctrl+N creates, Ctrl+E edits the selection, Delete removes a selected project or unused pipeline stage, Ctrl+R reloads the isolated local workspace, and Ctrl+I toggles details. Existing F1–F6 navigation remains unchanged. Ctrl+/ and the overflow-menu action open an in-app reference table.

These bindings do not bypass context, selection or access checks. Unsupported pages remain unchanged, mutation shortcuts still require administrator login, and deletion keeps its existing confirmation and relationship checks. Modal dialogs block the main-window shortcuts while open. Tests verify every binding, the help-table contents and the functional details toggle; the help dialog is captured during native Windows visual QA.
