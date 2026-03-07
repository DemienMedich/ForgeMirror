#define SourceRoot ".."

#ifndef AppVersion
  // Fallback to current app version (kept in CMakeLists.txt). Override via /DAppVersion=... when building.
  #define AppVersion "0.4.04"
#endif

#if AppVersion == "0.0.0"
  #pragma message "AppVersion is 0.0.0. Run installer/build-installer.ps1 (auto-reads APP_VERSION from CMakeLists.txt) or pass /DAppVersion=<version> to ISCC."
#endif

#ifndef OutputDir
  #define OutputDir SourceRoot + "\\dist"
#endif

[Setup]
AppId={{C4E4682B-6AC9-4B13-A9EA-4B9F4D780F8B}
AppName=ForgeMirror
AppVersion={#AppVersion}
AppVerName=ForgeMirror {#AppVersion}
AppPublisher=ForgeMirror
SetupIconFile={#SourceRoot}\installer\ForgeMirror.ico
DefaultDirName={autopf}\ForgeMirror
DefaultGroupName=ForgeMirror
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=ForgeMirrorSetup_{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
UninstallDisplayIcon={app}\ForgeMirrorGui.exe
VersionInfoVersion={#AppVersion}
VersionInfoTextVersion={#AppVersion}

[Tasks]
Name: "desktopicon"; Description: "Create a desktop icon"; Flags: unchecked

[Files]
Source: "{#SourceRoot}\build-gui\Release\ForgeMirrorGui.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\build-gui\Release\glfw3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "meta\shortcuts.json"
Source: "{#SourceRoot}\gui\fonts\*"; DestDir: "{app}\gui\fonts"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\ForgeMirror"; Filename: "{app}\ForgeMirrorGui.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\ForgeMirror"; Filename: "{app}\ForgeMirrorGui.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\ForgeMirrorGui.exe"; Description: "Launch ForgeMirror"; Flags: nowait postinstall skipifsilent






