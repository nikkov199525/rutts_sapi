#define MyAppName "ruTTS SAPI"
#define MyAppVersion GetDateTimeString('yyyy.mm.dd', '', '')
#define MyAppPublisher "ruTTS"

[Setup]
AppId={{7C535E29-541B-4CF6-B3DD-72E7A9689F31}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\ruTTS
DefaultGroupName=ruTTS
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir=Output
OutputBaseFilename=rutts_sapi_{#MyAppVersion}_setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\rutts_configurator.exe

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Files]
Source: "ru_tts\lib\x64\*"; DestDir: "{app}\lib\x64"; Flags: ignoreversion
Source: "ru_tts\lib\x32\*"; DestDir: "{app}\lib\x32"; Flags: ignoreversion
Source: "ru_tts\rutts_configurator.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "ru_tts\rulex.db"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autodesktop}\RuTTS configurator"; Filename: "{app}\rutts_configurator.exe"

[Run]
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\lib\x64\rutts_sapi.dll"""; StatusMsg: "Регистрация 64-битного SAPI-моста ruTTS..."; Flags: runhidden
Filename: "{syswow64}\regsvr32.exe"; Parameters: "/s ""{app}\lib\x32\rutts_sapi.dll"""; StatusMsg: "Регистрация 32-битного SAPI-моста ruTTS..."; Flags: runhidden

[UninstallRun]
Filename: "{sys}\regsvr32.exe"; Parameters: "/s /u ""{app}\lib\x64\rutts_sapi.dll"""; Flags: runhidden; RunOnceId: "UnregisterRuttsSapi64"
Filename: "{syswow64}\regsvr32.exe"; Parameters: "/s /u ""{app}\lib\x32\rutts_sapi.dll"""; Flags: runhidden; RunOnceId: "UnregisterRuttsSapi32"
