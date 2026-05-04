; Inno Setup Script for Launcher Widget
; ----------------------------------------------------------------------------
#define AppName "Launcher Widget"
#define AppVersion "1.0"
#define AppPublisher "Launcher Widget Contributors"
#define AppExeName "Launcher.exe"
#define AppId "{{8F2A4C3E-1D7B-4A9F-B6C2-3E5D8F1A2B4C}"

[Setup]
AppId={#AppId}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
AllowNoIcons=yes
OutputDir=bin\Installer
OutputBaseFilename=LauncherSetup
SetupIconFile=app.ico
Compression=lzma
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#AppExeName}

; スタートアップ登録を解除してからアンインストール
CloseApplications=yes

[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"

[Tasks]
Name: "desktopicon"; Description: "デスクトップにショートカットを作成する"; GroupDescription: "追加タスク:"; Flags: unchecked
Name: "startuprun"; Description: "Windows 起動時に自動で実行する"; GroupDescription: "追加タスク:"; Flags: unchecked

[Files]
Source: "bin\Release\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\{#AppName} のアンインストール"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
  ValueType: string; ValueName: "AntigravityLauncher"; \
  ValueData: """{app}\{#AppExeName}"""; \
  Flags: uninsdeletevalue; Tasks: startuprun

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Launcher Widget を起動する"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "taskkill"; Parameters: "/F /IM {#AppExeName}"; Flags: runhidden; RunOnceId: "KillLauncher"

[UninstallDelete]
Type: files; Name: "{app}\config.ini"
