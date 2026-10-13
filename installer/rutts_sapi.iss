; ruTTS SAPI installer. Packs out\ru_tts, which build_release.cmd lays out
; exactly as the files go into Program Files; every path below can be
; overridden with /D.
#define PRODUCT_NAME "ruTTS SAPI"
#define FOLDER_NAME "ruTTS"
; SetupVersion comes from the build; ISCC mangles /D names with underscores.
#ifdef SetupVersion
  #define PRODUCT_VERSION SetupVersion
#else
  #define PRODUCT_VERSION GetDateTimeString('yyyy.mm.dd', '', '')
#endif
#ifndef Payload
  #define Payload AddBackslash(SourcePath) + "..\out\ru_tts"
#endif
#ifndef OutputDir
  #define OutputDir AddBackslash(SourcePath) + "..\out\Output"
#endif

#define Configurator "rutts_configurator.exe"

[Setup]
AppId={{7C535E29-541B-4CF6-B3DD-72E7A9689F31}
AppName={#PRODUCT_NAME}
AppVersion={#PRODUCT_VERSION}
AppPublisher=ruTTS
DefaultDirName={autopf}\{#FOLDER_NAME}
DefaultGroupName={#PRODUCT_NAME}
DisableProgramGroupPage=yes
DirExistsWarning=no
; Both engines are installed so 32-bit (JAWS) and 64-bit SAPI clients see the
; voice; the configurator is a 64-bit build.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
; Screen readers keep rutts_sapi.dll loaded. Instead of closing the program the
; user hears the setup through, engines in use are renamed (MoveAside below) and
; replaced at once; restartreplace stays as the fallback if renaming fails.
CloseApplications=no
OutputDir={#OutputDir}
OutputBaseFilename=rutts_sapi_{#PRODUCT_VERSION}_setup
Compression=lzma2/ultra
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#Configurator}
UninstallDisplayName={#PRODUCT_NAME}

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[InstallDelete]
; Layout of earlier versions: engines in lib\x32 and lib\x64.
Type: filesandordirs; Name: "{app}\lib"
; Their desktop shortcut; this version offers its own (the desktopicon task).
Type: files; Name: "{autodesktop}\RuTTS configurator.lnk"
; Engines that MoveAside renamed while a SAPI host still had them loaded.
Type: files; Name: "{app}\x86\*.old"
Type: files; Name: "{app}\x64\*.old"
Type: files; Name: "{app}\*.old"

[Files]
Source: "{#Payload}\x86\ru_tts.dll"; DestDir: "{app}\x86"; BeforeInstall: MoveAside; Flags: ignoreversion restartreplace uninsrestartdelete 32bit
Source: "{#Payload}\x86\rutts_rulex.dll"; DestDir: "{app}\x86"; BeforeInstall: MoveAside; Flags: ignoreversion restartreplace uninsrestartdelete 32bit
Source: "{#Payload}\x86\rulex.dll"; DestDir: "{app}\x86"; BeforeInstall: MoveAside; Flags: ignoreversion restartreplace uninsrestartdelete 32bit
Source: "{#Payload}\x86\rutts_sapi.dll"; DestDir: "{app}\x86"; BeforeInstall: MoveAside; Flags: ignoreversion restartreplace uninsrestartdelete regserver 32bit
Source: "{#Payload}\x64\ru_tts.dll"; DestDir: "{app}\x64"; BeforeInstall: MoveAside; Flags: ignoreversion restartreplace uninsrestartdelete 64bit
Source: "{#Payload}\x64\rutts_rulex.dll"; DestDir: "{app}\x64"; BeforeInstall: MoveAside; Flags: ignoreversion restartreplace uninsrestartdelete 64bit
Source: "{#Payload}\x64\rulex.dll"; DestDir: "{app}\x64"; BeforeInstall: MoveAside; Flags: ignoreversion restartreplace uninsrestartdelete 64bit
Source: "{#Payload}\x64\rutts_sapi.dll"; DestDir: "{app}\x64"; BeforeInstall: MoveAside; Flags: ignoreversion restartreplace uninsrestartdelete regserver 64bit
; One database for both engines (the x86 and x64 trees build identical files):
; rutts_rulex.dll looks for it one level above its own folder. A
; running engine keeps it mapped: it cannot be overwritten, but it can be moved
; aside like the engines.
Source: "{#Payload}\rulex.db"; DestDir: "{app}"; BeforeInstall: MoveAside; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#Payload}\{#Configurator}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\LICENSES\*"; DestDir: "{app}\LICENSES"; Flags: ignoreversion recursesubdirs

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Icons]
Name: "{group}\Настройки {#PRODUCT_NAME}"; Filename: "{app}\{#Configurator}"; WorkingDir: "{app}"
Name: "{autodesktop}\Настройки {#PRODUCT_NAME}"; Filename: "{app}\{#Configurator}"; WorkingDir: "{app}"; Tasks: desktopicon
Name: "{group}\{cm:UninstallProgram,{#PRODUCT_NAME}}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\{#Configurator}"; Description: "Открыть настройки {#PRODUCT_NAME}"; Flags: nowait postinstall skipifsilent

[Code]
// A loaded DLL cannot be overwritten, but it can be renamed. Moving the old
// engine aside lets the new one be installed at once: a screen reader that
// keeps the old copy loaded picks up the new one on its next start, with no
// reboot. The renamed copy is deleted now if nothing uses it, else on reboot.
procedure MoveAside();
var
  FileName, OldName: String;
begin
  FileName := ExpandConstant(CurrentFileName);
  if not FileExists(FileName) then Exit;
  OldName := FileName + '.' + GetDateTimeString('yyyymmddhhnnss', #0, #0) + '.old';
  if RenameFile(FileName, OldName) then
  begin
    if not DeleteFile(OldName) then
      RestartReplace(OldName, '');
  end;
end;

// Earlier versions registered the engines from lib\x32 and lib\x64 with
// regsvr32. Their COM entries are overwritten by the new registration, so
// only the files have to go ([InstallDelete] above).

// Settings live in %APPDATA%\rutts and are kept unless the user says otherwise.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    if MsgBox('Удалить из папки пользователя настройки ruTTS?', mbConfirmation, MB_YESNO) = idYes then
      DelTree(ExpandConstant('{userappdata}\rutts'), True, True, True);
  end;
end;
