# Qt Widgets migration

## Branches

- `codex/pre-qt-2026-08-28`: exact stable ImGui snapshot, commit `7306152`, version 0.5.54.
- `codex/qt-gui`: incremental migration. `develop` and the ImGui implementation remain unchanged.

This is **stage 1**, not a feature-complete replacement for ImGui. The domain implementation and storage formats are reused without changes. The Qt preview deliberately retains base version 0.5.54; it is not a new stable release, and the existing installer still packages ImGui.

## Build and run

```powershell
.\build-qt.ps1 -Package
.\package-qt\ForgeMirrorQt.exe
```

Requires MSVC 2022, CMake and Qt 6.8+ Widgets/Test. Override the default installed Qt path using `-QtRoot`.

On first launch, Qt offers to **copy** the stable workspace to its own local application data directory (`Pharos/ForgeMirrorQt/workspace`). Cancel aborts startup; No starts an empty workspace. Existing Qt workspaces are never reimported automatically. A staged import prevents a partial copy from being treated as complete. Reparse/symlink entries are skipped. Do not edit the source while importing.

Qt never calls the cloud sync service. No Qt changes are written back to the original workspace. `FORGEMIRROR_STORAGE_DIR` identifies the source for the initial import, not Qt's output directory. A failed import may leave an `import-<uuid>` staging directory for inspection.

`--storage-dir <path>` opens an explicit disposable development workspace. Production directory paths, parents and children are rejected. Do not deliberately point it at other live storage folders. A lock prevents multiple Qt clients from editing the same Qt workspace.

Admin mutations require the existing admin password from the copied settings (the existing core's initial default for empty storage is `admin123`). Admin sessions are not persisted. The profile selector currently provides viewing, not migrated profile login/editing.

## Coverage

| Area | Qt stage 1 | Remaining |
| --- | --- | --- |
| Profiles | Profile selector, level/XP/rank, skills | Login, CRUD/archive, achievements, XP entry, wallet/spirits |
| Tasks | List, search, status filter, details, admin creation with project/category/skills/assignees/deadline/penalty/stage; New/In progress transitions and reopening | Full editing, bulk operations, reminders, delete rollback, transactional XP completion |
| Projects | Admin list and creation | Editing/deletion, embedded project focus |
| Skills | Catalog viewing and search | CRUD and profession filters |
| Pipeline | Stages and expanded details | Editing and task stage progression |
| Professions | Admin list | CRUD |
| Reports | Admin project metrics from existing report service | CSV export and full report UI |
| Audit | Admin task audit view | Other application logs |
| Other | Separate workspace, refresh, F1/F2/F3/F5/F6 | F4 rules, cloud, shortcuts, Pomodoro/vault, 3D, settings, migration installer |

Task completion is intentionally not exposed until the XP transaction flow is ported. Existing completed tasks are viewable. No placeholder mutation buttons pretend to perform missing functions.

## Verification

`build-qt.ps1 -Package` builds the Qt client and executes `smoke_qt` plus the existing `smoke_core`. Qt tests use a temporary workspace and exercise loading, search, status filtering, HTML escaping, administrator login/logout, project/task form persistence, status persistence and keyboard navigation.

For a packaged startup check (no installed Qt on PATH):

```powershell
.\package-qt\ForgeMirrorQt.exe --storage-dir Z:\CPP\ForgeMirror\build-qt\runtime-test --smoke-test --screenshot Z:\CPP\ForgeMirror\build-qt\qt-window.png
```

The screenshot/smoke flags are development diagnostics. They never default to the production workspace.
