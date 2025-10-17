# JobSkill

Console application in C++ for gamified skill profiles. Models team members, their skills, experience gain, and levels with a simple interactive CLI.

## Features
- Skill model with levels and experience, progressive XP curve.
- Profiles that aggregate skills and compute overall level.
- Profile issuing system (predefined blueprints) with login-based sessions.
- Auto-sync to local storage on every change, optional manual sync.
- Overall rank titles (Intern, Junior, Middle, Senior (+n)) derived from average level.

## Build
Use CMake (recommended) or the provided Makefile wrapper.

```bash
# Configure & build (Linux/macOS)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Windows (PowerShell, MSVC)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Artifacts appear in `build/JobSkill` (or `build/Release/JobSkill.exe` for MSVC).

Clean the build directory:

```bash
cmake -E rm -rf build
```

### Makefile wrapper

```bash
make              # Release build
make debug        # Debug build
make run          # Build + run
make clean        # Remove build directory
make rebuild      # Clean + configure + build (Release by default)
make rebuild BUILD_DIR=build CONFIG=Debug  # Example for Debug rebuild

# Override directory/config
make BUILD_DIR=out CONFIG=Debug build
```

## Run

```bash
./build/JobSkill              # Linux/macOS
./build/Release/JobSkill.exe  # Windows (MSVC)
```

### Profile storage
- Windows: `%APPDATA%\JobSkill\*.ini`
- Linux/macOS: `~/.jobskill/*.ini`

The files sit outside the build tree, so progress survives rebuilds.

### Windows encoding note
PowerShell/cmd use a legacy code page by default. Either switch to UTF-8 before running:

```powershell
chcp 65001
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new()
```

or run inside Windows Terminal/VS Code (UTF-8) — the app already sets UTF-8 via `SetConsoleOutputCP/SetConsoleCP`.

## Workflow

### Main menu commands
- `list` - show saved profiles with their numeric IDs and issued blueprints
- `skills` - display the global 3D skill catalog
- `archive <id>` / `restore <id>` - move a profile between active storage and the archive
- `delete <id>` - permanently remove a profile (confirmation required)
- `login <id|name>` - open an existing profile by ID or create/select by name
- `help` - quick reminder of commands
- `quit` - exit the application

### In-session commands (after login)
- `addxp <skill> <amount>` — grant XP to a skill (auto-sync afterwards)
- `show` — display the current profile
- `sync` - manual sync if auto-sync fails
- `logout` - return to the main menu
- `quit` - exit the application immediately

### Skill weights
- Support skills (e.g., Materials, UV Mapping, Props) carry weights around 0.8–0.9.
- Pipeline skills (Texturing, Shading, Lighting, Rendering) are near 1.0–1.1.
- Asset creation skills (Modeling, Sculpting, Hard Surface, Retopology, Environment) weigh ~1.2.
- Advanced production skills (Rigging, Animation, Simulation) weigh 1.3–1.4.
- Weights feed into a weighted average for the overall level and задаются по умолчанию (изменение не предусмотрено).

### Default profile
- Roman (Admin) - Modeling, Lighting, Materials (all start at level 1)

### Rank system
- 0–9: `Intern`
- 10–49: `Junior` (sub-levels every 10: 20→Junior (I), 30→Junior (II), ...)
- 50–149: `Middle` (sub-levels every 10: 60→Middle (I), 70→Middle (II), ...)
- 150+: `Senior` (sub-levels every 10: 160→Senior (I), 170→Senior (II), ...)

## Roadmap ideas
- Real storage format (e.g., JSON with nlohmann/json) or DB backend
- HTTP/gRPC server for real online sync and leaderboards
- Achievements, badges, specialization trees
- CLI polish (multi-word skill names, richer menus) or GUI front-end

## Admin notes
- The skill catalog is stored in `skills.txt` inside the profile directory (`%APPDATA%\JobSkill` / `~/.jobskill`).
- Default admin password is `admin123` (see `main.cpp`, constant `kAdminPassword`). Change it before using in production.
- Archived profiles are moved to the `archive/` subfolder under the same directory; use the CLI commands to restore or delete them.
- Profiles are persisted as `<id>.ini` files (e.g., `0001.ini`). IDs are auto-generated sequentially.

### ImGui GUI (optional)
To build the experimental ImGui-based desktop UI:
1. Install [GLFW](https://www.glfw.org/) and OpenGL development headers.
2. Clone [Dear ImGui](https://github.com/ocornut/imgui) and note the path (set `IMGUI_DIR`).
3. Configure with GUI support (vcpkg toolchain optional but recommended):
   ```bash
   make rebuild-gui GUI_BUILD_DIR=build-gui IMGUI_DIR=/path/to/imgui \
        VCPKG_CHAINFILE=C:/tools/vcpkg/scripts/buildsystems/vcpkg.cmake
   ```
4. Run `JobSkillGui` to launch the graphical interface (profiles list, details, XP actions).

The GUI uses the same storage/catalog data as the CLI and shows skill weights and ranks.


### ImGui GUI (optional)
To build the experimental ImGui-based desktop UI:
1. Install [GLFW](https://www.glfw.org/) and OpenGL development headers.
2. Clone [Dear ImGui](https://github.com/ocornut/imgui) and note the path (set `IMGUI_DIR`).
3. Configure with GUI support:
   ```bash
   cmake -S . -B build-gui -DBUILD_IMGUI_GUI=ON -DIMGUI_DIR=/path/to/imgui
   cmake --build build-gui --config Release --target JobSkillGui
   ```
4. Run `JobSkillGui` to launch the graphical interface (profiles list, details, XP actions).

The GUI uses the same storage/catalog data as the CLI and shows skill weights and ranks.


### ImGui GUI설 파이
