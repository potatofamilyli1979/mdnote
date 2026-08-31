; mdnote Windows installer script (Inno Setup 6/7).
;
; Expects a pre-built, self-contained deploy folder at dist\windows\
; (mdnote.exe plus its Qt6 and llvm-mingw runtime DLLs, and the
; platforms/styles plugin subfolders) -- windeployqt fails to auto-
; detect the platform plugin against this llvm-mingw Qt build (it
; misdetects the exe's debug/release build type and comes back with an
; empty plugin list, a known issue against non-MSVC-suffixed Qt builds),
; so that folder has to be assembled by hand. See README.md's Windows
; section for the exact steps (build, then copy the required DLLs/
; plugins listed there into dist\windows\).
;
; Build the installer with:
;   "C:\Program Files\Inno Setup 7\ISCC.exe" packaging\windows\mdnote.iss

#define MyAppName "mdnote"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "mdnote"
#define MyAppURL "https://github.com/potatofamilyli1979/mdnote"
#define MyAppExeName "mdnote.exe"

[Setup]
AppId={{DAEDCECE-8FF6-44A8-8201-B152F6DF0EC7}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
; Per-user by default (no UAC prompt) -- matches the app itself, a
; per-user utility that stores its own config via QSettings under the
; current user profile. PrivilegesRequiredOverridesAllowed still lets
; someone pick an all-users install from the wizard if they want one.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputDir=..\..\dist\installer
OutputBaseFilename=mdnote-{#MyAppVersion}-setup
SetupIconFile=..\..\data\windows\mdnote.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "chinesetraditional"; MessagesFile: "compiler:Languages\ChineseTraditional.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "..\..\dist\windows\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent
