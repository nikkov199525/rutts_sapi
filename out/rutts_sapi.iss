  #define AppName "rutts_sapi"
#define AppVersion "2026.10.02"
#define AppPublisher "nikkov199525"
#define InstallRootDir "rutts_sapi"
#define RuTtsCLSID "{{D2F6B2A1-7F5E-4B9A-9B5E-2B7C5B63F1C9}}"

[Setup]
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={pf}\{#InstallRootDir}
DisableDirPage=no
DisableProgramGroupPage=yes
DefaultGroupName={#AppName}
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64
OutputBaseFilename={#AppName}_{#AppVersion}_setup
LicenseFile=doc\README.txt
[Types]
Name: "full"; Description: "Полная установка"
Name: "x64only"; Description: "Только x64"
Name: "x86only"; Description: "Только x86"
Name: "custom"; Description: "Выборочная установка"; Flags: iscustom

[Components]
Name: "core"; Description: "Core"; Types: full x64only x86only custom; Flags: fixed
Name: "x64"; Description: "ruTTS SAPI (x64)"; Types: full x64only custom; Check: IsWin64
Name: "x86"; Description: "ruTTS SAPI (x86)"; Types: full x86only custom
Name: "dict"; Description: "Dictionary (rulex.db)"; Types: full custom
Name: "docs"; Description: "Документация"; Types: full custom
Name: "config"; Description: "Файл настроек (AppData)"; Types: full x64only x86only custom; Flags: fixed

[Dirs]
Name: "{app}\lib"
Name: "{app}\doc"
Name: "{userappdata}\rutts"

[Files]
Source: "doc\*"; DestDir: "{app}\doc"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: docs
Source: "ru_tts\lib\x64\*"; DestDir: "{app}\lib\x64"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: x64; Check: IsWin64
Source: "ru_tts\lib\x32\*"; DestDir: "{app}\lib\x32"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: x86
Source: "ru_tts\rulex.db"; DestDir: "{app}\lib"; Flags: ignoreversion; Components: dict
Source: "rutts\*"; DestDir: "{userappdata}\rutts"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: config

[Icons]
Name: "{group}\Открыть папку с документацией"; Filename: "explorer.exe"; Parameters: """{app}\doc"""; Components: docs
Name: "{group}\Открыть файл настроек"; Filename: "{cmd}"; Parameters: "/c start """" ""{userappdata}\rutts\rutts.ini"""; WorkingDir: "{userappdata}\rutts"; Components: config

[Run]
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\lib\x64\rutts_sapi.dll"""; Flags: runhidden; Components: x64; Check: IsWin64
Filename: "{syswow64}\regsvr32.exe"; Parameters: "/s ""{app}\lib\x32\rutts_sapi.dll"""; Flags: runhidden; Components: x86; Check: IsWin64
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\lib\x32\rutts_sapi.dll"""; Flags: runhidden; Components: x86; Check: not IsWin64

[UninstallRun]
Filename: "{sys}\regsvr32.exe"; Parameters: "/s /u ""{app}\lib\x64\rutts_sapi.dll"""; Flags: runhidden; Components: x64; Check: IsWin64
Filename: "{syswow64}\regsvr32.exe"; Parameters: "/s /u ""{app}\lib\x32\rutts_sapi.dll"""; Flags: runhidden; Components: x86; Check: IsWin64
Filename: "{sys}\regsvr32.exe"; Parameters: "/s /u ""{app}\lib\x32\rutts_sapi.dll"""; Flags: runhidden; Components: x86; Check: not IsWin64
