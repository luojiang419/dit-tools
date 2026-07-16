#ifndef AppVersion
#define AppVersion "0.1.97"
#endif

#ifndef VersionTag
#define VersionTag "v0.1.97"
#endif

#ifndef SourceDir
#define SourceDir "."
#endif

#ifndef OutputDir
#define OutputDir "."
#endif

[Setup]
AppId={{F3C5C6D6-8B77-4F6A-9F6B-9EA5B6D6AA21}
AppName=影资管家
AppVersion={#AppVersion}
AppVerName=影资管家 {#VersionTag}
AppPublisher=DIT Tools
SetupIconFile=..\..\dit-tools-src\cinevault-pro\resources\icons\app.ico
DefaultDirName={autopf}\影资管家
DefaultGroupName=影资管家
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=CineVault-Setup-{#VersionTag}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
UninstallDisplayIcon={app}\CineVault.exe
SetupLogging=yes
PrivilegesRequired=admin
CloseApplications=no
RestartApplications=no

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加快捷方式："; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "data\*"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\data\models\*"; DestDir: "{app}\data\models"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\影资管家"; Filename: "{app}\CineVault.exe"
Name: "{group}\卸载影资管家"; Filename: "{uninstallexe}"
Name: "{autodesktop}\影资管家"; Filename: "{app}\CineVault.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\CineVault.exe"; Description: "启动影资管家"; Flags: nowait postinstall skipifsilent

[Code]
var
  UpdateProgressFilePath: String;
  LastReportedInstallProgress: Integer;

function IsCineVaultRunning(): Boolean;
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{cmd}'),
    '/C tasklist /FI "IMAGENAME eq CineVault.exe" /NH | find /I "CineVault.exe" >NUL 2>NUL',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := ResultCode = 0;
end;

function StopCineVaultProcesses(const Phase: String): Boolean;
var
  Attempt: Integer;
  ResultCode: Integer;
begin
  Result := True;

  if not IsCineVaultRunning() then
    Exit;

  Log('CineVault process detected during ' + Phase + '; forcing shutdown before installation continues.');

  for Attempt := 1 to 3 do
  begin
    Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /T /IM CineVault.exe',
      '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Sleep(500);

    if not IsCineVaultRunning() then
      Exit;
  end;

  MsgBox('检测到旧版影资管家仍在运行，安装程序无法自动结束进程。请手动退出影资管家后重新运行安装包。',
    mbError, MB_OK);
  Result := False;
end;

function ShouldForceStopCineVaultProcesses(): Boolean;
begin
  { The independent updater is also a copied CineVault.exe. Its progress-file
    argument identifies that path so the installer does not kill its host tree. }
  Result := UpdateProgressFilePath = '';
end;

function InitializeSetup(): Boolean;
begin
  UpdateProgressFilePath := ExpandConstant('{param:UPDATEPROGRESSFILE|}');
  LastReportedInstallProgress := -1;

  if ShouldForceStopCineVaultProcesses() then
    Result := StopCineVaultProcesses('setup initialization')
  else
    Result := True;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if ShouldForceStopCineVaultProcesses() then
  begin
    if not StopCineVaultProcesses('file installation') then
      Result := '旧版影资管家进程仍在运行，安装已取消。';
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;

  if CurPageID = wpReady then
  begin
    if ShouldForceStopCineVaultProcesses() then
      Result := StopCineVaultProcesses('ready page');
  end;
end;

function InitializeUninstall(): Boolean;
begin
  Result := StopCineVaultProcesses('uninstall initialization');
end;

procedure CurInstallProgressChanged(CurProgress, MaxProgress: Integer);
var
  InstallProgress: Integer;
begin
  if (UpdateProgressFilePath = '') or (MaxProgress <= 0) then
    Exit;

  InstallProgress := Round((CurProgress / MaxProgress) * 100);
  if InstallProgress < 0 then
    InstallProgress := 0
  else if InstallProgress > 100 then
    InstallProgress := 100;

  if InstallProgress <> LastReportedInstallProgress then
  begin
    SaveStringToFile(UpdateProgressFilePath, IntToStr(InstallProgress), False);
    LastReportedInstallProgress := InstallProgress;
  end;
end;
