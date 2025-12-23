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

Now:
- No active task; awaiting new requests.

Next:
- TBD based on user request (e.g., refine catalog UX, re-enable decay with config, further localization).

Open questions:
- Need confirmation if decay should ever return and under what rules (UNCONFIRMED).
- Should achievement XP bonuses apply in CLI flows (`task`/`addxp`) the same way as GUI?
- Should repeat/recovery penalties affect skill XP or only global XP?
- When adding new skills to the catalog, should existing profiles auto-add them at level 1/0 XP?
- When deleting a profile, should its achievements JSON be removed too?

Working set (files/ids/commands):
- gui/GuiApp.cpp, src/SkillCatalog.cpp, include/SkillCatalog.h, src/FileStorage.cpp, main.cpp; data/skills.txt; builds via `cmake --build build --config Release` and `cmake --build build-gui --config Release --target JobSkillGui`.
