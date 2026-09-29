# ForgeMirror Qt 0.6.86 release verification

- Canonical version: root `VERSION` = `0.6.86`.
- Scope: stage 241 fixes Qt informational command-line startup. `--help`/`-h` print registered options, `--version`/`-v` print the canonical version, and unknown options exit 2 before workspace or GUI startup.
- Windows per-user installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.86.exe`; 12,527,524 bytes; FileVersion/ProductVersion `0.6.86`; SHA-256 `316F7B2691E9AD4094CC9779C24EB2BA62107F4AE1933683E22DF3BD5A910418`.
- Packaged Qt executable: `Z:\CPP\ForgeMirror\package-qt-0.6.86-release\ForgeMirrorQt.exe`; 3,599,360 bytes; FileVersion/ProductVersion `0.6.86`; SHA-256 `E712DA4E036DDD716F6AB6CCA182F53950E990DECD2AD6CBD017150704791CAC`.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.86-release` passed. `smoke_qt` passed 1/1 in 27.21 seconds and `smoke_core` reported `OK`. Regression tests launch the real executable in separate processes and verify all four help/version spellings, unknown-option rejection, and no implicit workspace creation.
- Packaged checks ran with `PATH` restricted to Windows system directories and Qt plugin environment variables removed. `--help` exited 0 and emitted 880 bytes including `--storage-dir`; `--version` exited 0 with `ForgeMirrorQt 0.6.86`; an unknown option exited 2 with a diagnostic. No default workspace was created. A disposable `--storage-dir ... --smoke-test --screenshot ...` launch exited 0 and saved `Z:\CPP\ForgeMirror\build-qt\cli-smoke-0.6.86-35AFEA412A8E4F5BA8FAF49C05F3EBA1\window.png` (52,107 bytes; SHA-256 `63887669524A996704607885B405FAFC70A05B779F37EA4928463F68733FC91C`).
- Inno Setup 6.7.3 compiled the per-user installer. Its FileVersion and ProductVersion match `0.6.86`.
- `installer/verify-qt-lifecycle.ps1 -PreviousVersion 0.6.85 -CurrentVersion 0.6.86` passed in a disposable installation using test-only AppId `{9BA03E9B-C9B7-493D-AD8F-A213CAAE1E22}` and path `C:\Users\mrdem\AppData\Local\Programs\ForgeMirrorQtLifecycle-9BA03E9BC9B7493DAD8FA213CAAE1E22`. It installed 0.6.85, updated the same install to 0.6.86, and verified EXE/registry versions plus version output and window titles with Qt removed from `PATH`. Isolated uninstall exited 0 and removed the app folder and uninstall registration. External workspace marker SHA-256 remained `7BB6463B30F9E301FED333CDF8960CA9497B602CCD8EEB46AE42693FDEA15A4D`.
- Lifecycle evidence: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.86-9BA03E9BC9B7493DAD8FA213CAAE1E22`. The test-only 0.6.85 and 0.6.86 installers were 12,527,534 bytes (SHA-256 `FDD2CF290249D4D649D30BC079C80718C9D8BBF1CB49098CEF7EF594427E4F62`) and 12,527,602 bytes (SHA-256 `49CCD78B6CD0D005467F1B0C7FA17B7717A3138801F943DCA2D2F2A7FD4BABAB`). Lifecycle screenshots: 0.6.85, 52,424 bytes, SHA-256 `386D8F2D523E2B201BE2B69BF839CEFD6597048F9B659F8A56E55C89A3BC5541`; 0.6.86, 52,058 bytes, SHA-256 `1BA9CF3342DD54259BA37338C574E5DE1E065FE1EFFAF5283B05A68D2F35AE58`.
- The user-edited `AgentsSkills/CONTINUITY.md` was kept separate from this release.

Functional migration remains an expert estimate of about 95%, not a measured code or test percentage. The broader action-level parity audit remains open; this checkpoint does not declare the Qt port complete.
