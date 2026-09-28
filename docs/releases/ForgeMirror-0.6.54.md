# ForgeMirror Qt 0.6.54 release verification

- Canonical version: `VERSION` = `0.6.54`.
- Scope: safer first-run import from the stable workspace. The importer stages a file snapshot, skips links/reparse points, hashes source and staged files, rechecks the source before publishing and refuses overlapping roots or a destination that already exists.
- Production installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.54.exe`; 11,556,648 bytes; SHA-256 `165B55421B9F08DFE5FE4905F9AA5660F7FE09EEB3FB88189A9914184AE677B5`.
- Packaged executable: `Z:\CPP\ForgeMirror\package-qt-0.6.54\ForgeMirrorQt.exe`; 3,352,576 bytes; FileVersion/ProductVersion `0.6.54`; SHA-256 `A24579A39EE956224B106888661885E049132B3DDC7B05E0B6E077D7B40205E0`.
- Installer FileVersion/ProductVersion: `0.6.54`. Compiled with Inno Setup 6.7.3. Production AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; per-user install and user data remain separate.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.54` completed; `smoke_qt` passed 1/1, including workspace import snapshot tests; `smoke_core: OK`.
- `ForgeMirrorGui` (stable ImGui) compiled successfully; no ImGui UI source was changed.
- Packaged EXE opened the real window and exited 0 with `PATH=C:\Windows\System32;C:\Windows`, Qt plugin environment overrides removed, and isolated workspace. Screenshot: `Z:\CPP\ForgeMirror\build-qt\package-smoke-0.6.54\window.png` (51,525 bytes).
- Test-only AppId `{D6CFA14D-5C4B-4D53-99A5-35545E3352D8}` lifecycle: 0.6.53 installed and 0.6.54 updated in place; EXE ProductVersion changed from `0.6.53` to `0.6.54`. Installed 0.6.54 passed real-window smoke with the minimal Windows `PATH` (exit 0; screenshot `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.54\installed-window.png`, 51,684 bytes). Silent uninstall exited 0, removed the test EXE, and preserved `workspace/user-data-marker.txt`.
- The test AppId, installation, registry identity, workspace and environment were isolated; the production AppId and current-user installation were not used in the lifecycle test.

## Migration coverage

- Stage 209 prevents an inconsistent source copy from becoming the active Qt workspace; failed attempts clean up only their own staging directory.
- Import regression tests verify nested file equality, untouched source bytes, symlink omission when the host permits creating one, existing destination preservation, overlapping-root refusal and missing-source refusal.
- Functional parity remains an expert estimate; external writers that ignore core save APIs, multi-file third-party transactions and hands-on NVDA/JAWS review remain open.
