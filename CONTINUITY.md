Goal (incl. success criteria):
- Keep JobSkill CLI/GUI stable while fixing gameplay/UX issues (Russian locale-friendly text, correct XP logic, editable skill catalog); builds in Release must succeed.

Constraints/Assumptions:
- Locale/formatting: force classic/UTF-8 to avoid garbled numbers/strings; GUI/CLI must stay Russian-friendly.
- Degradation system currently disabled (kDecayEnabled=false) and should stay off unless explicitly re-enabled.
- Skill catalog persists to data/skills.txt; profiles stored in data/*.ini via FileStorage.

Key decisions:
- Disabled category decay/buffer recomputation (both CLI and GUI) via kDecayEnabled=false.
- Gameplay rules parsing uses classic locale and sanitized numbers; penalties remain active.
- GUI/CLI logs use classic locale to avoid non-ASCII separators.
- GUI supports adding/removing skills (persists catalog and cleans profiles on delete).

State:
- Builds: Release targets JobSkill.exe and JobSkillGui.exe compile clean.
- Storage: FileStorage now writes/reads numbers locale-independently; recoveryTasks preserved across restarts.
- Documentation: Added docs/USER_GUIDE.md (RU) describing gameplay rules, XP system, skill catalog management, storage, and CLI/GUI usage.
- Access control: Admin profile seeded as "Admin" (admin flag persisted); admin password `admin123` required for full access. CLI/GUI default to read-only until admin login.

Done:
- Fixed level progress bar math (uses total needed XP).
- Applied gameplay bonuses/penalties from saved rules.
- Stopped resetting levels when rules saved.
- Prevented recoveryTasks reset; sanitized FileStorage numeric parsing.
- Disabled degradation system.
- Added skill catalog add/delete UI and catalog removal from profiles.
- Normalized log formatting to ASCII/classic locale.
- Added admin gating: CLI command `admin` to toggle admin mode; non-admin users can только смотреть. GUI adds admin login/logout, disables mutations (profiles, XP, rules, skills) until authenticated.

Now:
- No active task; awaiting new requests.

Next:
- TBD based on user request (e.g., refine catalog UX, re-enable decay with config, further localization).

Open questions:
- Need confirmation if decay should ever return and under what rules (UNCONFIRMED).

Working set (files/ids/commands):
- gui/GuiApp.cpp, src/SkillCatalog.cpp, include/SkillCatalog.h, src/FileStorage.cpp, main.cpp; data/skills.txt; builds via `cmake --build build --config Release` and `cmake --build build-gui --config Release --target JobSkillGui`.
