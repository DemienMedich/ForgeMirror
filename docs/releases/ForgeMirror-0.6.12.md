# ForgeMirror 0.6.12 Qt installer verification

- Canonical version: root `VERSION` = `0.6.12`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.12.exe` (11,506,864 bytes).
- SHA-256: `005B92E6200852A4D6922DA911B90E7120B112056CD636623E5779289E47388A`.
- Setup ProductVersion: `0.6.12`; application ProductVersion/FileVersion and `--version`: `0.6.12`.
- Stable AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; per-user install (`PrivilegesRequired=lowest`), no elevation.
- Previous installer used for update verification: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.11.exe`.
- Package payload inventory: 28 EXE/DLL/plugin/config files; no workspace, `meta` directory, admin settings or developer data.

## Verification on 2026-09-28

Evidence directory: `Z:\CPP\ForgeMirror\build-qt\installer-test-stage166-5bdc325c6fae479faff11bc71ec66586`.

1. Installed 0.6.11 into an isolated application directory; setup exit code 0 and installed EXE ProductVersion was 0.6.11.
2. Created a separate user-data marker, then installed 0.6.12 over 0.6.11 at the same path; setup exit code 0 and the marker remained unchanged.
3. Verified HKCU uninstall `DisplayVersion=0.6.12`, installed EXE ProductVersion/FileVersion `0.6.12`, and `--version` output `ForgeMirrorQt 0.6.12`.
4. With `PATH=C:\Windows\System32;C:\Windows` and Qt plugin environment overrides removed, the installed real-window `--smoke-test` exited 0, created `installed-window.png` (40,976 bytes), and had empty stderr.
5. Ran the installed uninstaller with exit code 0. The EXE and HKCU uninstall entry were removed; the separate user-data marker remained unchanged.

The build passed `smoke_qt` (1/1) and `smoke_core: OK`. The packaged build reported `ForgeMirrorQt 0.6.12`. Release scope is the Qt migration branch `codex/qt-gui`; stable ImGui and `develop` were not changed. This installer was not published through PharosHub; no Hub manifest was changed.

## 0.6.12 scope

Qt migration checkpoint 166 adds privacy-safe administrator audit outcomes for project and catalog operations. See `data/meta/patch-notes/0.6.12.md`.
