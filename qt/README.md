# Qt Widgets migration

## Branches

- `codex/pre-qt-2026-08-28`: exact stable ImGui snapshot, commit `7306152`, version 0.5.54.
- `codex/qt-gui`: incremental migration. `develop` and the ImGui implementation remain unchanged.

This is **stage 6**, not a feature-complete replacement for ImGui. Existing storage formats and domain services are reused. The Qt-only `AppTaskCompletionService` adds transactional task XP completion without changing the stable ImGui implementation. The Qt preview deliberately retains base version 0.5.54; it is not a new stable release, and the existing installer still packages ImGui.

## Build and run

Design direction for subsequent UI work: [user-supplied interface references](../docs/design/INTERFACE_REFERENCES.md).
These guide composition and hierarchy; the existing dark/purple palette is unchanged.

```powershell
.\build-qt.ps1 -Package
.\package-qt\ForgeMirrorQt.exe
```

Requires MSVC 2022, CMake and Qt 6.8+ Widgets/Test. Override the default installed Qt path using `-QtRoot`.

On first launch, Qt offers to **copy** the stable workspace to its own local application data directory (`Pharos/ForgeMirrorQt/workspace`). Cancel aborts startup; No starts an empty workspace. Existing Qt workspaces are never reimported automatically. A staged import prevents a partial copy from being treated as complete. Reparse/symlink entries are skipped. Do not edit the source while importing.

Qt never calls the cloud sync service. No Qt changes are written back to the original workspace. `FORGEMIRROR_STORAGE_DIR` identifies the source for the initial import, not Qt's output directory. A failed import may leave an `import-<uuid>` staging directory for inspection.

`--storage-dir <path>` opens an explicit disposable development workspace. Production directory paths, parents and children are rejected. Do not deliberately point it at other live storage folders. A lock prevents multiple Qt clients from editing the same Qt workspace.

Admin mutations require the existing admin password from the copied settings (the existing core's initial default for empty storage is `admin123`). Admin sessions are not persisted. Profile viewing remains available without a personal login session. Profile management is administrator-only; normal password changes require the current profile password. Full personal login/access-policy migration remains outstanding.

## Coverage

| Area | Qt stage 6 | Remaining |
| --- | --- | --- |
| Profiles | Selector, compact level/XP/task metrics, skills, admin creation/archive/restore, profession/spirit/block editing, password reset/change | Personal login/access policies, rename/delete, achievements, standalone XP entry, wallet operations |
| Tasks | List, search, status filter, details and awarded XP, admin creation with project/category/skills/assignees/deadline/penalty/stage; status transitions, transactional metadata editing and XP completion | Bulk operations, reminders, delete rollback |
| Projects | Admin list, creation and editing; stable IDs and current names in linked tasks | Deletion, embedded project focus |
| Skills | Catalog viewing/search, admin creation and editing with checked atomic persistence | Delete/merge, profession assignment and filters |
| Pipeline | Stages, details, admin creation/editing including next-step links; current names in task rows | Guided task progression, deletion and reordering |
| Professions | Admin list | CRUD |
| Reports | Admin project metrics from existing report service | CSV export and full report UI |
| Audit | Admin task audit view | Other application logs |
| Other | Separate workspace, refresh, F1/F2/F3/F5/F6 | F4 rules, cloud, shortcuts, Pomodoro/vault, 3D, settings, migration installer |

### Profile management

On the Profile page, an administrator can open **Управление профилями**. The searchable list includes an optional archive view. Creation uses the existing service and generates a login/password; credentials are masked until explicitly revealed and are not copied to the clipboard or written to logs. Store them before closing the manager.

Editing changes profession, spirit and blocked state in a single profile save, preserving XP, wallet, credentials and history. Unknown existing profession IDs remain selectable rather than being silently erased. Archiving requires confirmation, reports assigned active tasks, and preserves the profile and task history; restore is reversible. Archived profiles cannot be edited or receive XP. Permanent deletion and renaming are not exposed yet.

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

Skills support name, description, category and weight (0.5–1.6). Renaming preserves the skill ID, profile files and existing profession links. Existing profile XP and weights are not recalculated. Descriptions are required and single-line because the legacy text format cannot safely encode arbitrary multiline fields; pipe and control characters are rejected. New skills do not receive profession links in this increment.

The legacy catalog writer does not return an I/O result. Qt therefore edits a temporary copy, reloads and compares every record, then uses QSaveFile with direct-write fallback disabled to replace skills.txt atomically. A changed source or failed verification/write leaves the original intact. Some legacy metadata combinations (notably category plus profession) cannot round-trip through the existing parser; Qt rejects such edits rather than silently losing metadata. Repairing that shared format is deferred. Do not edit the workspace externally during a save.

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