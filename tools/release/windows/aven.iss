; The Windows installer (Inno Setup 6): Aven-<version>-Setup.exe.
;
; iscc /DVersion=0.4.0 /DNumericVersion=0.4.0 /DSource=dist\aven-0.4.0-windows-x64 /DOutput=dist tools\release\windows\aven.iss
;
; Installs for the current user (no administrator needed) into %LOCALAPPDATA%\Programs\Aven, with
; Start menu and optional desktop shortcuts and an uninstaller. The files are the same as in the zip,
; START HERE.txt included, so the editor's updater keeps this copy up to date in place.

#ifndef Version
  #define Version "0.0.0"
#endif
; Numbers only (0.4.0 for 0.4.0-beta.1), for the installer's file version.
#ifndef NumericVersion
  #define NumericVersion "0.0.0"
#endif
#ifndef Source
  #error Pass /DSource=<the release folder>
#endif
#ifndef Output
  #define Output "."
#endif

[Setup]
AppId={{6F6D3C2B-3E1A-4B8E-9E36-6A0B5F1D2C47}
AppName=Aven
AppVersion={#Version}
AppVerName=Aven {#Version}
AppPublisher=Aven
AppPublisherURL=https://github.com/ethandadev/Aven
AppSupportURL=https://github.com/ethandadev/Aven/issues
AppUpdatesURL=https://github.com/ethandadev/Aven/releases
VersionInfoVersion={#NumericVersion}.0
VersionInfoDescription=Aven installer
DefaultDirName={localappdata}\Programs\Aven
DefaultGroupName=Aven
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#Output}
OutputBaseFilename=Aven-{#Version}-Setup
SetupIconFile=..\..\..\resources\icon\aven.ico
UninstallDisplayIcon={app}\aven-editor.exe
UninstallDisplayName=Aven
LicenseFile=..\..\..\LICENSE
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#Source}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Aven"; Filename: "{app}\aven-editor.exe"; Comment: "Make 2D and 3D games"
Name: "{autodesktop}\Aven"; Filename: "{app}\aven-editor.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\aven-editor.exe"; Description: "{cm:LaunchProgram,Aven}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Folders the updater may have replaced or added to since installing (never anything else in {app}).
Type: filesandordirs; Name: "{app}\.aven-update"
Type: filesandordirs; Name: "{app}\templates"
Type: filesandordirs; Name: "{app}\data"
Type: filesandordirs; Name: "{app}\quests"
Type: filesandordirs; Name: "{app}\sdk"
Type: filesandordirs; Name: "{app}\web"
Type: filesandordirs; Name: "{app}\players"
