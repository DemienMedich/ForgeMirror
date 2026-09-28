# ForgeMirror 0.6.38 Qt installer verification

- Canonical version: root `VERSION` = `0.6.38`.
- Installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.38.exe`.
- Size: `11,541,150` bytes.
- SHA-256: `EA71D993E6D1D22BCB6F88756381D4BF9E1E18120C6573174F893BFF8196A707`.
- Setup ProductVersion/FileVersion and packaged EXE ProductVersion: `0.6.38`.
- Production AppId: `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; current-user install (`PrivilegesRequired=lowest`).
- Payload: 28 files.

## Verification

- `build-qt.ps1 -Package` passed; `smoke_qt` 1/1 passed and `smoke_core: OK`.
- New recovery regression preserves changed in-flight workspace files before startup rollback and verifies that the preservation directory remains reported after workspace initialization/reload.
- Production installer compiled with Inno Setup 6.7.3. It emits the existing non-blocking `[UninstallRun]` warning about `RunOnceId`.
- Isolated lifecycle used test-only AppId `{6F5F26B1-2A77-4BD1-8719-CA6F075A0E43}` under `build-qt/installer-lifecycle-e84595d1b61043c4bfde7506cfbf91cb/isolated-install`: installed 0.6.37, upgraded in place to 0.6.38 at the same path, verified uninstall `DisplayVersion=0.6.38` and EXE ProductVersion `0.6.38`, then silently uninstalled. Installer and registration were removed; separate user-data marker and test workspace remained.
- Installed test EXE started with Qt directories removed from `PATH` (`C:\Windows\System32;C:\Windows`), no Qt plugin environment overrides, `--smoke-test` exit code 0; screenshot `build-qt/installer-lifecycle-e84595d1b61043c4bfde7506cfbf91cb/isolated-startup-waited.png` (54,383 bytes).
- Production 0.6.37 installation and user workspace were not changed by this lifecycle test.

## Scope

Stage 193 saves changed in-flight files to a recovery snapshot before startup rollback of an interrupted generic multi-file transaction. The Qt status bar and local log identify the snapshot folder. Immediate in-process rollback remains unchanged. Other Qt migration parity gaps remain open.