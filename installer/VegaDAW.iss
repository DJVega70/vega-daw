; Vega Productions DAW - Windows installer (Inno Setup 6)
; Built by GitHub Actions: iscc /DAppVersion=0.4.0.<build> /DSrcDir=<dist folder> installer\VegaDAW.iss

#ifndef AppVersion
  #define AppVersion "0.4.0"
#endif
#ifndef SrcDir
  #define SrcDir "..\dist"
#endif

[Setup]
AppId={{6B1E2C4A-5F7D-4E8B-9A3C-1D2E3F4A5B6C}
AppName=Vega Productions DAW
AppVersion={#AppVersion}
AppVerName=Vega Productions DAW {#AppVersion}
AppPublisher=Vega Productions
DefaultDirName={autopf}\Vega Productions\DAW
DefaultGroupName=Vega Productions
DisableProgramGroupPage=yes
LicenseFile={#SrcDir}\LICENSE
OutputDir=..\installer-out
OutputBaseFilename=VegaProductions-DAW-Setup
SetupIconFile=..\Resources\VegaDAW.ico
UninstallDisplayIcon={app}\Vega Productions DAW.exe
UninstallDisplayName=Vega Productions DAW
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
ChangesAssociations=yes
CloseApplications=yes
PrivilegesRequiredOverridesAllowed=dialog

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Shortcuts:"
Name: "assoc"; Description: "Open .vegaproj projects with Vega Productions DAW"; GroupDescription: "Files:"

[Files]
Source: "{#SrcDir}\Vega Productions DAW.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\VegaStemHelper.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\VegaStemHelperAVX2.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\TRACKTION_LICENSE.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Vega Productions DAW"; Filename: "{app}\Vega Productions DAW.exe"
Name: "{group}\Uninstall Vega Productions DAW"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Vega Productions DAW"; Filename: "{app}\Vega Productions DAW.exe"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.vegaproj"; ValueType: string; ValueName: ""; ValueData: "VegaProductions.Project"; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\VegaProductions.Project"; ValueType: string; ValueName: ""; ValueData: "Vega Productions DAW project"; Flags: uninsdeletekey; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\VegaProductions.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\Vega Productions DAW.exe,0"; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\VegaProductions.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Vega Productions DAW.exe"" ""%1"""; Tasks: assoc

[Run]
Filename: "{app}\Vega Productions DAW.exe"; Description: "Start Vega Productions DAW"; Flags: nowait postinstall skipifsilent
