# ForgeMirror Qt 0.6.88 release verification

- Canonical version: root `VERSION` = `0.6.88`.
- Scope: first restrained-motion visual pass. The sidebar selection marker moves locally over 180 ms; the preference is stored per workspace, and Windows client-area animation settings are honored. Palette, density, page structure, and feature behavior are unchanged.
- Windows per-user installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.88.exe`; 12,564,803 bytes; FileVersion/ProductVersion `0.6.88`; SHA-256 `25B34C45BD7D2C8D97F1E08A5F292BCFC27DB4111FF630044CD53716619BE666`.
- Packaged Qt executable: `Z:\CPP\ForgeMirror\package-qt-0.6.88-release\ForgeMirrorQt.exe`; 3,727,360 bytes; FileVersion/ProductVersion `0.6.88`; SHA-256 `F38AC9FA90A0D2024CE48C35D662D23C439D29FD0CB1D2A78FA3C1DBFB462642`.
- Windows platform plugin: `Z:\CPP\ForgeMirror\package-qt-0.6.88-release\platforms\qwindows.dll`.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.88-release` passed. `smoke_qt` passed 1/1 in 32.14 seconds; `smoke_core` reported `OK`.
- The new Qt regression checks save/load of the motion preference, the settings checkbox, reset behavior, legacy workspaces defaulting to motion enabled, parity with the current Windows client-area animation preference, and immediate marker repositioning when motion is disabled. Final `smoke_qt` passed 1/1 in 36.59 seconds with these assertions.
- Packaged startup was checked with `PATH=C:\Windows\System32;C:\Windows`, `QT_PLUGIN_PATH` and `QT_QPA_PLATFORM_PLUGIN_PATH` unset. `--version` printed `ForgeMirrorQt 0.6.88`; a real window reported `ForgeMirror · Qt migration · 0.6.88` and the isolated smoke exited successfully. The screenshot is `Z:\CPP\ForgeMirror\build-qt\package-smoke-0.6.88-b02c56098ad54b45af97e36dcf946086\window.png` (SHA-256 `1951FADC29AF4ECB1DB711EA137F683F0893F48C0237FA263A650A0C70908F87`).
- Inno Setup 6.7.3 built the actual current-user installer at the path above; its EXE metadata was checked against canonical `VERSION`.
- `installer/verify-qt-lifecycle.ps1 -PreviousVersion 0.6.87 -CurrentVersion 0.6.88` passed under a disposable AppId and installation directory. It installed 0.6.87, updated the same isolated installation to 0.6.88, then launched and uninstalled it with Qt removed from `PATH`. The installed EXE, version output, window title, uninstall entry, and bundled platform plugin were checked by the script.
- Isolated uninstall exited 0 and removed its application directory and registry entry. The external workspace marker remained byte-identical (SHA-256 `7BB6463B30F9E301FED333CDF8960CA9497B602CCD8EEB46AE42693FDEA15A4D`). Lifecycle evidence: `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.88-25D6DA2948B948F085A77EC3BC4EED22`.
- The user's existing installation was not modified. ImGui baseline remains `7306152` / 0.5.54.
