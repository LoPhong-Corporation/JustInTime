#define AppName "JustInTime"
#define AppVersion "1.0.2.1"
#define AppPublisher "LoPhong Corporation"
#define BuildDir "..\out\installer-build\bin\Release"

[Setup]
AppId={{B563BFA6-8C70-45D5-9D87-AC4EA5F052E8}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL=https://lonamphong.com
DefaultDirName={autopf}\JustInTime
DefaultGroupName=JustInTime
UninstallDisplayIcon={app}\JustInTime.exe
LicenseFile=LICENSE.txt
SetupIconFile=..\resources\JustInTime.ico
OutputDir=dist
OutputBaseFilename=JustInTime-Setup-{#AppVersion}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
Compression=lzma2
SolidCompression=yes
Uninstallable=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#BuildDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.pdb;*.ilk;*.exp;*.lib;*.obj;*.idb;*.ipch;*.tlog;*.lastbuildstate"
Source: "..\JITDashboard\src-tauri\target\release\jitdashboard.exe"; DestDir: "{app}"; Flags: ignoreversion; DestName: "jitdashboard.exe"

[Icons]
Name: "{group}\JustInTime"; Filename: "{app}\JustInTime.exe"; WorkingDir: "{app}"
Name: "{group}\Usage Dashboard"; Filename: "{app}\jitdashboard.exe"; WorkingDir: "{app}"
Name: "{group}\Native Dashboard"; Filename: "{app}\JustInTimeDashboard.exe"; WorkingDir: "{app}"
Name: "{group}\Uninstall JustInTime"; Filename: "{uninstallexe}"
Name: "{autodesktop}\JustInTime"; Filename: "{app}\JustInTime.exe"; WorkingDir: "{app}"; Tasks: desktopicon
Name: "{autodesktop}\Usage Dashboard"; Filename: "{app}\jitdashboard.exe"; WorkingDir: "{app}"; Tasks: desktopicon
Name: "{autodesktop}\Native Dashboard"; Filename: "{app}\JustInTimeDashboard.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Microsoft Visual C++ Runtime..."; Flags: runhidden waituntilterminated
Filename: "{app}\JustInTime.exe"; Description: "Launch JustInTime"; Flags: postinstall nowait skipifsilent
