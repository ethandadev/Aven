; The Windows installer (Inno Setup 6): Rynax-<version>-Setup.exe.
;
; iscc /DVersion=0.4.0 /DNumericVersion=0.4.0 /DSource=dist\rynax-0.4.0-windows-x64 /DOutput=dist tools\release\windows\rynax.iss
;
; Installs for the current user (no administrator needed) into %LOCALAPPDATA%\Programs\Rynax, with
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
AppName=Rynax
AppVersion={#Version}
AppVerName=Rynax {#Version}
AppPublisher=Rynax
AppPublisherURL=https://github.com/ethandadev/Rynax
AppSupportURL=https://github.com/ethandadev/Rynax/issues
AppUpdatesURL=https://github.com/ethandadev/Rynax/releases
VersionInfoVersion={#NumericVersion}.0
VersionInfoDescription=Rynax installer
DefaultDirName={localappdata}\Programs\Rynax
DefaultGroupName=Rynax
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#Output}
OutputBaseFilename=Rynax-{#Version}-Setup
SetupIconFile=..\..\..\resources\icon\rynax.ico
UninstallDisplayIcon={app}\rynax-editor.exe
UninstallDisplayName=Rynax
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
Name: "{autoprograms}\Rynax"; Filename: "{app}\rynax-editor.exe"; Comment: "Make 2D and 3D games"
Name: "{autodesktop}\Rynax"; Filename: "{app}\rynax-editor.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\rynax-editor.exe"; Description: "{cm:LaunchProgram,Rynax}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Folders the updater may have replaced or added to since installing (never anything else in {app}).
Type: filesandordirs; Name: "{app}\.rynax-update"
Type: filesandordirs; Name: "{app}\templates"
Type: filesandordirs; Name: "{app}\data"
Type: filesandordirs; Name: "{app}\quests"
Type: filesandordirs; Name: "{app}\sdk"
Type: filesandordirs; Name: "{app}\web"
Type: filesandordirs; Name: "{app}\players"
