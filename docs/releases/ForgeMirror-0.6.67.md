# ForgeMirror Qt 0.6.67 release verification

- Canonical version: root `VERSION` = `0.6.67`.
- Scope: stage 222. Banner mutations now reject stale edit/delete snapshots, merge new additions, and refuse malformed JSON; vault settings refresh the latest balance and journal before saving. Both flows use the shared workspace lock and privacy-safe core events.
- Windows installer: `Z:\CPP\ForgeMirror\dist\ForgeMirrorSetup_0.6.67.exe`; 11,568,202 bytes; SHA-256 `B8E9498F82E988392A51603274DDA1FE813214802D74373681C015E1647E3025`.
- Installer FileVersion/ProductVersion: `0.6.67`; built with Inno Setup 6.7.3. Production AppId remains `{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}`; installation is per-user.
- Packaged executable: `Z:\CPP\ForgeMirror\package-qt-0.6.67\ForgeMirrorQt.exe`; 3,405,312 bytes; FileVersion/ProductVersion `0.6.67`; SHA-256 `104FF98A4175FA4D4271C62F4FF7F0CC96703649365807D12B00DAEA60D09D10`.
- ImGui compatibility executable rebuilt from the same `VERSION`: `Z:\CPP\ForgeMirror\build-gui\Release\ForgeMirrorGui.exe`; 2,973,184 bytes; FileVersion/ProductVersion `0.6.67`; SHA-256 `75EBA4D60384ECEBE0040CC702AC425D8BCA3EAA22C53045A3881920965305AA`.

## Verification

- `build-qt.ps1 -Package -PackageDirectory package-qt-0.6.67` passed. `smoke_qt` passed 1/1 in 38.22 seconds and `smoke_core` reported `OK`.
- Packaged `ForgeMirrorQt.exe --version` reported `ForgeMirrorQt 0.6.67` with `PATH=C:\Windows\System32;C:\Windows` and Qt environment overrides removed. Its real-window `--smoke-test` exited 0 with empty stderr and saved `Z:\CPP\ForgeMirror\build-qt\package-smoke-0.6.67-889bdd19773f448b92d45df755397f51\window.png` (52,304 bytes).
- Temporary lifecycle installers used the same application files with an isolated AppId `{703AACB1-563C-457A-A26F-479953C74F4D}` and install directory `Z:\CPP\ForgeMirror\build-qt\lifecycle-0.6.67-568f7ca5afc24d2781c847777d2c0253\install`; production AppId/installation were not used.
- The isolated 0.6.66 installer installed successfully; its EXE and HKCU uninstall `DisplayVersion` reported `0.6.66`. The 0.6.67 installer updated the same directory and AppId in place; its EXE and uninstall record reported `0.6.67`.
- Both installed executables passed real-window smoke with Qt removed from `PATH`: exit 0 for versions 0.6.66 and 0.6.67. Screenshots are `...\smoke\installed-0.6.66.png` and `...\smoke\installed-0.6.67.png` in the lifecycle directory.
- The isolated user's external workspace contained 7 files / 2,263 bytes before update and after update; both manifests had SHA-256 `A6D80700ED8A99E88370E1C4B394B642CCA869F476FCD0E9DE76EC2C45CEE616`. After the 0.6.67 smoke, its 7-file / 2,721-byte manifest was `83CA354610BDFF0A80D0568DCA9571942F687F474D488E844AEF9FDC0515E464`; after uninstall it was identical. Uninstall exited 0, removed the isolated executable and HKCU uninstall entry, and left the external marker unchanged (SHA-256 `A06FEB32E5C65FC9FE966E09D415C00D02BDA3ED8ED0A197783EAAF241C1E584`).
- The package directory contains no developer `data` directory. No PharosHub manifest is used by this checkout; no Hub manifest was changed.
- The production installation was not updated during this verification: its 0.6.66 process remained open. `develop` and `origin/develop` remain at `7306152c603ff8007200f64e63c4188510d55588`.

The Qt migration remains an expert estimate of about 90% complete, not a code or test percentage. Hands-on NVDA/JAWS interaction certification, wider non-UI core event coverage, and coordination with older or external writers that bypass the shared lock remain open.
