# JobSkill

Gamified skill-profile tracker in C++ with both a console client and an optional ImGui desktop GUI. Track skills, record task scores, and grow overall rank while the system enforces category mastery rules.

## Highlights
- Weighted skill model with progressive XP curve and overall level.
- Task evaluation by category (E–A) and score (1–10); repeat scores grant skill XP while overall XP is awarded only for new personal bests.
- Percent-based skill distribution sheet with automatic 100 % balancing.
- Category history stored per profile; Senior rank unlocks only after scoring 10/10 in every category.
- Local storage with automatic sync, optional manual sync, and shared data between CLI and GUI.

## Build
Use CMake (preferred). The CLI has no external dependencies; the GUI additionally requires OpenGL, GLFW, and Dear ImGui (vcpkg recommended on Windows).

```powershell
# CLI (MSVC)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# Optional GUI (MSVC + vcpkg)
cmake -S . -B build-gui `
  -DBUILD_IMGUI_GUI=ON `
  -DIMGUI_DIR="Z:/CPP/JobSkill/libs/imgui" `
  -DCMAKE_TOOLCHAIN_FILE="C:/tools/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DVCPKG_PWSH_PATH="C:/Program Files/PowerShell/7/pwsh.exe"
cmake --build build-gui --config Release --target JobSkillGui
```

On Linux/macOS replace the generator line with `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` and omit the Windows-specific flags for the GUI.

Artifacts end up in `build/Release/JobSkill.exe` (CLI) and `build-gui/Release/JobSkillGui.exe` (GUI). Clean with `cmake -E rm -rf build build-gui`.

### Makefile wrapper (CLI)
```bash
make              # Release build
make debug        # Debug build
make run          # Build + run
make rebuild      # Clean + configure + build (Release)
```

## Run
```bash
./build/Release/JobSkill.exe        # Windows CLI
./build-gui/Release/JobSkillGui.exe # Windows GUI
```
Linux/macOS binaries live under `build/JobSkill` and `build-gui/JobSkillGui` respectively.

### Storage
- Windows: `%APPDATA%\JobSkill\*.ini`
- Linux/macOS: `~/.jobskill/*.ini`

Profiles persist outside the build tree, so progress survives rebuilds. Category best scores are stored in each profile’s `[categories]` section.

### Encoding (Windows)
The console app switches the code page to UTF‑8 via `SetConsoleOutputCP/SetConsoleCP`, but you can also run `chcp 65001` before launching if the terminal shows mojibake.

## CLI workflow
### Main menu
- `list` — show saved profiles and issued templates.
- `skills` — display the skill catalog and weights.
- `archive <id>` / `restore <id>` — toggle archived state.
- `delete <id>` — remove a profile permanently (confirmation required).
- `login <id|name>` — open an existing profile by ID or create/select by name.
- `quit` — exit the application.

### Session commands
- `addxp <skill> <amount>` — grant XP to a specific skill (integer amount).
- `show` — print the active profile, including category bests.
- `sync` — force-save if auto-sync fails.
- `logout` — return to main menu.

## GUI workflow
The ImGui front-end mirrors the CLI data with additional panels:
- Profiles list with archive toggle.
- Detailed profile view showing levels, category scores, and rank status.
- Add Experience modal:
  - Pick task category (E–A) and score (1–10).
  - Sliders auto-redistribute percentages between skills so the total remains 100 %.
  - Repeating an existing best grants skill XP only; beating it grants overall XP and updates the stored best. Hitting 10 finalises the category and logs mastery.

## Rank system
- Level < 10: `Intern`
- 10 – 49: `Junior (I/II/…)` every 10 levels.
- 50 – 149: `Middle (I/II/…)` every 10 levels.
- ≥ 150: `Senior (I/II/…)`, but the title unlocks only if all categories are mastered (score 10). Otherwise it displays `Middle … (Senior locked: ...)` with the outstanding categories.

## Admin notes
- Default admin profile “Roman” is created automatically with basic skills; password is `admin123` (see `main.cpp`).
- Skills catalogue lives in `skills.txt` under the storage directory.
- Archived profiles move to `%APPDATA%\JobSkill\archive` (`~/.jobskill/archive` on *nix).
- Profiles are INI files (`0001.ini`, `0002.ini`, …); IDs auto-increment.

## Roadmap ideas
- JSON or DB storage backend.
- Networked API for syncing/leaderboards.
- Achievement and badge system.
- Richer CLI input (multi-word skills) and extended GUI dashboards.
