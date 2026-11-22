#ifndef AppExe
#define AppExe "..\..\build\privdot.exe"
#endif

[Setup]
AppId={{7E8C3E6B-4A6D-4B61-9B18-2D4E9C6F5A12}
AppName=privdot
AppVersion=1.0.0
AppPublisher=vxqzn
AppPublisherURL=https://github.com/vxqzn/privdot
DefaultDirName={localappdata}\privdot
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputBaseFilename=privdot-setup
SetupIconFile=privdot.ico
UninstallDisplayIcon={app}\privdot.exe
UninstallDisplayName=privdot
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
AppMutex=Local\privdot_single_instance

[Tasks]
Name: "autostart"; Description: "Start privdot with Windows"; Flags: checkedonce

[Files]
Source: "{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\privdot"; Filename: "{app}\privdot.exe"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "privdot"; ValueData: """{app}\privdot.exe"""; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\privdot.exe"; Description: "Launch privdot"; Flags: nowait postinstall skipifsilent
