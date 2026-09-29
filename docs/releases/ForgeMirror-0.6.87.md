# ForgeMirror Qt 0.6.87 release verification

- Canonical version: root `VERSION` = `0.6.87`.
- Scope: release of the completed mapped functional migration through stage 268. Qt covers all 18 legacy workspace tabs; the action-level parity audit found no known missing user-facing workflow. This is a checklist result, not a claim that every internal implementation detail or manual accessibility check is identical.
- Windows per-user installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.87.exe`; 12,562,587 bytes; FileVersion/ProductVersion `0.6.87`; SHA-256 `F7A8CE835944B66160F3D51761D3953F87BE5F934D2075A9EE91D8DB8FA051E9`.
- Packaged Qt executable: `Z:\CPP\ForgeMirror\package-qt-0.6.87-release\ForgeMirrorQt.exe`; 3,720,704 bytes; FileVersion/ProductVersion `0.6.87`; SHA-256 `79F5E19B4B6BB4BBAC77502A9963693D97C643BCF3C3667DE793460C5E6337D3`.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.87-release` passed. `smoke_qt` passed 1/1 in 31.51 seconds; `smoke_core` reported `OK`. Coverage includes the migrated action workflows, real administrator login dialog, remember-session persistence across three fresh processes, password retention, and window startup.
- Inno Setup 6.7.3 built the actual current-user installer. Installer FileVersion and ProductVersion match `0.6.87`; its filename is derived from the canonical `VERSION`.
- `installer/verify-qt-lifecycle.ps1 -PreviousVersion 0.6.86 -CurrentVersion 0.6.87` passed under a disposable AppId and install directory. It installed 0.6.86, updated that same installation to 0.6.87, and checked EXE/uninstall-registry versions, `--version`, and real GUI window titles/screenshots. The applications launched with `PATH` restricted to Windows system folders and Qt plugin environment variables removed, exercising the bundled `platforms/qwindows.dll` deployment.
- Isolated uninstall exited 0, removed the app directory and uninstall registration, and left the external workspace marker byte-identical (SHA-256 `7BB6463B30F9E301FED333CDF8960CA9497B602CCD8EEB46AE42693FDEA15A4D`).
- Lifecycle evidence: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.87-BE6F969BE6BB4553987676F75133741F`. Installed 0.6.86 and 0.6.87 window screenshots were 52,330 bytes (SHA-256 `E6246D1FD49BC84D23030D19C6B142F0EFB4D30EE7EB775A5E910A09035C9757`) and 52,680 bytes (SHA-256 `4CEBB0883076548EC4CDA8847A08F388BEBC6A1C1DA7862FC15FD38BDDF8582E`). The isolated test installers were 12,527,602 bytes (SHA-256 `78F256060CA8A8D25DDA96B531FD965BB10D194A0A581E33CFC77631B7D0EFD5`) and 12,562,663 bytes (SHA-256 `EAF7F0F1EAE122D5EDD042688E15B44775449A359F915CE972EEF101DB1A8DB8`).
- The user-edited `AgentsSkills/CONTINUITY.md` was kept outside the release changes.

The mapped user-facing Qt migration is complete. Follow-up work is hardening rather than unported feature work: hands-on NVDA/JAWS testing, core-event coverage outside instrumented Qt workflows, and compatibility with external writers that bypass the shared workspace lock. The preserved ImGui baseline remains version 0.5.54.
