# Qt Widgets migration

## Branches

- `codex/pre-qt-2026-08-28`: exact stable ImGui snapshot, commit `7306152`, version 0.5.54.
- `codex/qt-gui`: incremental migration. `develop` and the ImGui implementation remain unchanged.

This is **stage 3**, not a feature-complete replacement for ImGui. Existing storage formats and domain services are reused. The Qt-only `AppTaskCompletionService` adds transactional task XP completion without changing the stable ImGui implementation. The Qt preview deliberately retains base version 0.5.54; it is not a new stable release, and the existing installer still packages ImGui.

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

| Area | Qt stage 3 | Remaining |
| --- | --- | --- |
| Profiles | Selector, compact level/XP/task metrics, skills, admin creation/archive/restore, profession/spirit/block editing, password reset/change | Personal login/access policies, rename/delete, achievements, standalone XP entry, wallet operations |
| Tasks | List, search, status filter, details and awarded XP, admin creation with project/category/skills/assignees/deadline/penalty/stage; status transitions and transactional XP completion | Full editing, bulk operations, reminders, delete rollback |
| Projects | Admin list and creation | Editing/deletion, embedded project focus |
| Skills | Catalog viewing and search | CRUD and profession filters |
| Pipeline | Stages and expanded details | Editing and task stage progression |
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
