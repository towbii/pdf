#define AppName      "PDF Editor"
#define AppVersion   "1.5.3"
#define AppPublisher "towbii"
#define AppURL       "https://github.com/towbii/pdf"
#define AppExeName   "PDFEditor.exe"
#define BuildDir     "..\cpp\build\Release"

[Setup]
AppId={{A3F2B8C1-4D5E-4F6A-B7C8-D9E0F1A2B3C4}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
AllowNoIcons=yes
LicenseFile=..\LICENSE
OutputDir=output
OutputBaseFilename=PDFEditor-Setup
SetupIconFile=..\cpp\src\PDFEditor.ico
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
CloseApplicationsFilter=PDFEditor.exe
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[InstallDelete]
; Only cleans the install directory (Program Files\PDF Editor).
; User data in %APPDATA%\PDFEditor\ (signatures, settings, recent files)
; is NEVER touched — {app} and {userappdata} are completely separate paths.
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\imageformats"
Type: filesandordirs; Name: "{app}\iconengines"
Type: filesandordirs; Name: "{app}\styles"
Type: filesandordirs; Name: "{app}\tls"
Type: filesandordirs; Name: "{app}\networkinformation"
Type: filesandordirs; Name: "{app}\generic"
Type: filesandordirs; Name: "{app}\translations"
Type: files;          Name: "{app}\*.dll"
Type: files;          Name: "{app}\*.qm"

[Code]
// Silently uninstall previous version before installing new one.
// This removes old DLLs and exe cleanly, while keeping user data in AppData.
procedure CleanPreviousVersion();
var
  UninstStr: String;
  ResultCode: Integer;
begin
  if RegQueryStringValue(HKCU,
    'Software\Microsoft\Windows\CurrentVersion\Uninstall\{A3F2B8C1-4D5E-4F6A-B7C8-D9E0F1A2B3C4}_is1',
    'UninstallString', UninstStr) then
  begin
    UninstStr := RemoveQuotes(UninstStr);
    Exec(UninstStr, '/SILENT /NORESTART /SUPPRESSMSGBOXES', '', SW_HIDE,
         ewWaitUntilTerminated, ResultCode);
  end;
end;

function InitializeSetup(): Boolean;
begin
  CleanPreviousVersion();
  Result := True;
end;

[Tasks]
Name: "desktopicon";    Description: "Create a &desktop shortcut";       GroupDescription: "Additional icons:"
Name: "fileassoc";      Description: "Open .pdf files with PDF Editor";  GroupDescription: "File associations:"

[Files]
; Main executable
Source: "{#BuildDir}\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion

; Qt core DLLs
Source: "{#BuildDir}\Qt6Core.dll";       DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Qt6Gui.dll";        DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Qt6Widgets.dll";    DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Qt6Network.dll";    DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Qt6Svg.dll";        DestDir: "{app}"; Flags: ignoreversion

; Runtime DLLs
Source: "{#BuildDir}\brotlicommon.dll";  DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#BuildDir}\brotlidec.dll";     DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#BuildDir}\bz2.dll";           DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#BuildDir}\freetype.dll";      DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#BuildDir}\harfbuzz.dll";      DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#BuildDir}\jpeg62.dll";        DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#BuildDir}\libpng16.dll";      DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#BuildDir}\openjp2.dll";       DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#BuildDir}\z.dll";             DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#BuildDir}\opengl32sw.dll";    DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#BuildDir}\D3Dcompiler_47.dll";DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

; Qt plugins
Source: "{#BuildDir}\platforms\*";        DestDir: "{app}\platforms";        Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\imageformats\*";     DestDir: "{app}\imageformats";     Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\iconengines\*";      DestDir: "{app}\iconengines";      Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\styles\*";           DestDir: "{app}\styles";           Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\tls\*";              DestDir: "{app}\tls";              Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\networkinformation\*"; DestDir: "{app}\networkinformation"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\generic\*";          DestDir: "{app}\generic";          Flags: ignoreversion recursesubdirs skipifsourcedoesntexist

; Translations
Source: "{#BuildDir}\en.qm";             DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#BuildDir}\translations\*";    DestDir: "{app}\translations";     Flags: ignoreversion recursesubdirs skipifsourcedoesntexist

[Icons]
Name: "{group}\{#AppName}";              Filename: "{app}\{#AppExeName}"
Name: "{group}\Uninstall {#AppName}";   Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}";        Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Registry]
; .pdf file association
Root: HKCU; Subkey: "Software\Classes\.pdf";                    ValueType: string; ValueName: ""; ValueData: "PDFEditor.Document"; Flags: uninsdeletevalue; Tasks: fileassoc
Root: HKCU; Subkey: "Software\Classes\PDFEditor.Document";      ValueType: string; ValueName: ""; ValueData: "PDF Document";       Flags: uninsdeletekey;   Tasks: fileassoc
Root: HKCU; Subkey: "Software\Classes\PDFEditor.Document\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExeName},0"; Tasks: fileassoc
Root: HKCU; Subkey: "Software\Classes\PDFEditor.Document\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeName}"" ""%1"""; Tasks: fileassoc

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent
