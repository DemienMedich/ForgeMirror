# ForgeMirror Qt 0.6.53 release verification

- Canonical version: `VERSION` = `0.6.53`.
- Scope: Qt achievement read-modify-write operations now share `meta/profile-write.lock` with `FileStorage` profile writes and lifecycle operations. Legacy binaries and external tools that ignore the lock remain outside the guarantee.
- Production installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.53.exe`; 11,555,009 bytes; SHA-256 `DC01DE696E161AF4E904A27A9C67EC1C752470A0ADEA84C7C2B9657C00302083`.
- Packaged executable: `Z:\CPP\ForgeMirror\package-qt-0.6.53\ForgeMirrorQt.exe`; 3,345,408 bytes; FileVersion/ProductVersion `0.6.53`; SHA-256 `3E01BCCFB6B904F8D008ECEC8058044B7BBD5D38A95F39C578744AFA46E37B3E`.
- Installer FileVersion/ProductVersion: `0.6.53`. Compiled with Inno Setup 6.7.3. Production AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; per-user install and user data remain separate.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.53` completed; `smoke_qt` passed 1/1 (including lock contention refusal, unchanged achievement bytes, then successful retry); `smoke_core: OK`.
- `ForgeMirrorGui` (stable ImGui) compiled successfully against the extracted shared lock source; no ImGui UI code changed.
- Packaged EXE opened the real window and exited 0 with `PATH=C:\Windows\System32;C:\Windows`, Qt plugin environment overrides removed, and isolated workspace. Screenshot: `Z:\CPP\ForgeMirror\build-qt\package-smoke-0.6.53\window.png` (51,504 bytes).
- Test-only AppId `{D6CFA14D-5C4B-4D53-99A5-35545E3352D8}` lifecycle: 0.6.52 installed and 0.6.53 updated in place; EXE ProductVersion changed from `0.6.52` to `0.6.53`. Silent uninstall exited 0, removed the test EXE, and preserved `workspace/user-data-marker.txt`. The installed 0.6.53 EXE also passed real-window smoke with the minimal Windows `PATH` (exit 0; screenshot `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.53\installed-window.png`, 51,444 bytes), then uninstalled cleanly.
- Lifecycle used isolated install, registry identity, workspace and environment; the production AppId and production installation were not used.

## Migration coverage

- Stage 208 closes the current Qt achievement sidecar bypass of the profile write lock.
- The Qt achievement mutation test confirms a cooperating writer holding the lock prevents grant/edit/revoke from changing sidecar bytes; retry succeeds after release.
- Migration remains an expert estimate of about 90%; it is not a percentage derived from line counts. External/non-cooperating writers, cross-file external transactions, broader core telemetry and hands-on NVDA/JAWS review remain open.
