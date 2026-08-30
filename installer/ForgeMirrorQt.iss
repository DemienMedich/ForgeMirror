#define SourceRoot ".."

#ifndef AppVersion
  #error AppVersion must be passed from installer/build-qt-installer.ps1
#endif

#ifndef OutputDir
  #define OutputDir SourceRoot + "\\dist"
#endif

[Setup]
AppId={{8B99E76B-4510-49D8-AE45-9DDF85EA21DC}
AppName=ForgeMirror
AppVersion={#AppVersion}
AppVerName=ForgeMirror {#AppVersion}
AppPublisher=Pharos
SetupIconFile={#SourceRoot}\installer\ForgeMirror.ico
DefaultDirName={localappdata}\Programs\ForgeMirror
DefaultGroupName=ForgeMirror
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=ForgeMirrorSetup_{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\ForgeMirrorQt.exe
VersionInfoVersion={#AppVersion}
VersionInfoTextVersion={#AppVersion}
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: "desktopicon"; Description: "Создать ярлык на рабочем столе"; Flags: unchecked

[Files]
Source: "{#SourceRoot}\package-qt\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\ForgeMirror"; Filename: "{app}\ForgeMirrorQt.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\ForgeMirror"; Filename: "{app}\ForgeMirrorQt.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\ForgeMirrorQt.exe"; Description: "Запустить ForgeMirror"; Flags: nowait postinstall skipifsilent
