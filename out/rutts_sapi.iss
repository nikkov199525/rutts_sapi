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
VersionInfoVersion={#MyAppVersion}.0
VersionInfoProductName={#MyAppName}
VersionInfoCompany={#MyAppPublisher}

[Files]
Source: "ru_tts\lib\x32\ru_tts.dll"; DestDir: "{app}\lib\x32"; Flags: ignoreversion
Source: "ru_tts\lib\x32\rulex.dll"; DestDir: "{app}\lib\x32"; Flags: ignoreversion
Source: "ru_tts\lib\x32\rutts_rulex.dll"; DestDir: "{app}\lib\x32"; Flags: ignoreversion
Source: "ru_tts\lib\x32\rutts_sapi.dll"; DestDir: "{app}\lib\x32"; Flags: ignoreversion regserver 32bit
Source: "ru_tts\lib\x64\ru_tts.dll"; DestDir: "{app}\lib\x64"; Flags: ignoreversion
Source: "ru_tts\lib\x64\rulex.dll"; DestDir: "{app}\lib\x64"; Flags: ignoreversion
Source: "ru_tts\lib\x64\rutts_rulex.dll"; DestDir: "{app}\lib\x64"; Flags: ignoreversion
Source: "ru_tts\lib\x64\rutts_sapi.dll"; DestDir: "{app}\lib\x64"; Flags: ignoreversion regserver 64bit
Source: "ru_tts\rutts_configurator.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "ru_tts\rulex.db"; DestDir: "{app}"; Flags: ignoreversion
Source: "ru_tts\doc\*"; DestDir: "{app}\doc"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\ruTTS Configurator"; Filename: "{app}\rutts_configurator.exe"; WorkingDir: "{app}"

[Run]
Filename: "{app}\rutts_configurator.exe"; Description: "Открыть конфигуратор ruTTS"; Flags: nowait postinstall skipifsilent
