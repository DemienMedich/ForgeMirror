# ForgeMirror Qt 0.6.50 installer verification

- Canonical version: root `VERSION` = `0.6.50`.
- Production installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.50.exe`.
- Installer size: 11,554,274 bytes; SHA-256: `D3791CBA41588ED10B3A37CE02C3A6EB722B7FE9ED841C60DACBE231435DAF6B`.
- Packaged EXE: `Z:\CPP\ForgeMirror\package-qt-next10\ForgeMirrorQt.exe`, 3,341,824 bytes; FileVersion/ProductVersion `0.6.50`; SHA-256: `8CA479DDC1EFC74C937AA58C5E979C9C4609AD4AC6D0B783DEB80871C7E1C32A`.
- Installer ProductVersion: `0.6.50`. Compiled with Inno Setup 6.7.3. Production AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; per-user installation and persistent user data.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-next10` passed. `smoke_qt` passed 1/1; `smoke_core: OK`; Qt runtime deployment completed.
- Packaged EXE real-window `--smoke-test --screenshot` exited 0 with `PATH=C:\Windows\System32;C:\Windows`, Qt plugin environment overrides cleared, and separate `LOCALAPPDATA`, `APPDATA`, and workspace. Screenshot: `Z:\CPP\ForgeMirror\build-qt\package-smoke-current\window.png` (51,014 bytes).
- Production installer compiled by `installer/build-qt-installer.ps1 -PackageDirectory .\package-qt-next10`; installer and application metadata both report `0.6.50`.
- Isolated lifecycle used test-only AppId `{68D81375-72C8-413C-8AF5-4572B719662B}`, test install directory `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.50\installed`, and separate data/profile directories. Installed 0.6.49, then updated in place to 0.6.50. Installed EXE and HKCU uninstall `DisplayVersion` both reported `0.6.50`; a user-data marker survived update.
- Installed 0.6.50 passed real-window smoke with the minimal Windows PATH and cleared Qt environment overrides (exit 0); screenshot: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.50\installed-window.png` (51,540 bytes).
- Silent uninstall exited 0, removed the test EXE and uninstall registry entry, and preserved the user-data marker. Production install at 0.6.45 and its data were not touched.

## Scope

Stage 205 moves privacy-safe commit/rollback telemetry for task creation, metadata editing, plain deletion, and deletion of an awarded task record while preserving XP into the journaled task service. Smoke verifies committed and rollback outcomes and checks that private task/profile data is not logged. Functional migration remains an expert estimate of about 90%, not a measured code/test percentage. Remaining gaps include hands-on NVDA/JAWS interaction testing, some core events outside instrumented Qt workflows, the optimistic task compare/replace race, and live external writes during multi-file transactions. Stable ImGui and `develop` remain unchanged.
