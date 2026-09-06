#ifndef FluxSourceDir
  #define FluxSourceDir "..\..\..\out\dist\windows-x64\Flux Installer"
#endif
#ifndef FluxSetupOutputDir
  #define FluxSetupOutputDir "..\..\..\out\dist\windows-x64\Flux Installer Setup"
#endif
#ifndef FluxAppVersion
  #define FluxAppVersion "2026 - v0.8.19-1-alpha"
#endif
#ifndef FluxNumericVersion
  #define FluxNumericVersion "0.8.19.1"
#endif
#ifndef FluxOutputBaseFilename
  #define FluxOutputBaseFilename "Flux_Suite_Setup_2026_-_v0.8.19-1-alpha_windows-x64"
#endif

[Setup]
AppId={{5CB6E94C-6922-4CC7-A4D4-AB70A7F9F4E2}
AppName=Flux Suite
AppVersion={#FluxAppVersion}
AppVerName=Flux Suite {#FluxAppVersion}
AppPublisher=OMNIATV
AppPublisherURL=https://omniatv.com
AppSupportURL=https://software.omniatv.com
AppUpdatesURL=https://software.omniatv.com/flux-suite/windows-x64/manifest.json
DefaultDirName={autopf}\Flux Suite
DefaultGroupName=Flux Suite
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
UsePreviousPrivileges=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#FluxSetupOutputDir}
OutputBaseFilename={#FluxOutputBaseFilename}
SetupIconFile=..\resources\icons\flux-suite.ico
UninstallDisplayIcon={app}\Flux Suite.exe
UninstallDisplayName=Flux Suite
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern dark includetitlebar hidebevels
WizardSizePercent=110
WizardSmallImageFile=..\resources\icons\flux-suite-wizard.png
WizardSmallImageBackColor=none
DisableWelcomePage=no
DisableFinishedPage=no
CloseApplications=yes
RestartApplications=no
UsePreviousAppDir=yes
SetupLogging=yes
VersionInfoVersion={#FluxNumericVersion}
VersionInfoCompany=OMNIATV
VersionInfoDescription=Flux Suite installer and updater
VersionInfoProductName=Flux Suite
VersionInfoProductVersion={#FluxNumericVersion}
VersionInfoCopyright=Copyright (C) 2026 OMNIATV
MinVersion=10.0.17763

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Messages]
PrivilegesRequiredOverrideTitle=Flux Suite installation
PrivilegesRequiredOverrideInstruction=Choose who can use Flux Suite
PrivilegesRequiredOverrideText2=Install Flux Suite for your account only, or make it available system-wide for everyone who uses this computer.
PrivilegesRequiredOverrideAllUsers=Install for &all users (requires administrator approval)
PrivilegesRequiredOverrideCurrentUser=Install for &me only

[Files]
Source: "{#FluxSourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\resources\icons\flux-suite.ico"; DestName: "flux-suite.ico"; Flags: dontcopy

[Icons]
Name: "{group}\Flux Suite"; Filename: "{app}\Flux Suite.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Flux Suite"; Filename: "{app}\Flux Suite.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\Flux Suite.exe"; Description: "Launch Flux Suite"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: dirifempty; Name: "{app}"

[Code]
const
  FluxBackground = $00161314;
  FluxPanel = $00251E20;
  FluxText = $00F8F3F4;
  FluxMuted = $00B3A6AA;
  FluxAccent = $00F55572;
  FluxUninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{5CB6E94C-6922-4CC7-A4D4-AB70A7F9F4E2}_is1';

function RemovePreviousSuite(RootKey: Integer): Boolean;
var
  Uninstaller: String;
  ClosingQuote: Integer;
  ResultCode: Integer;
begin
  Result := True;
  if not RegQueryStringValue(RootKey, FluxUninstallKey,
      'UninstallString', Uninstaller) then
    exit;

  Uninstaller := Trim(Uninstaller);
  if (Length(Uninstaller) > 1) and (Uninstaller[1] = '"') then
  begin
    Delete(Uninstaller, 1, 1);
    ClosingQuote := Pos('"', Uninstaller);
    if ClosingQuote > 0 then
      SetLength(Uninstaller, ClosingQuote - 1);
  end;
  if not FileExists(Uninstaller) then
    exit;

  Result := Exec(Uninstaller,
    '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART', '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  { A fixed AppId is stored in a different registry hive for per-user and
    all-user installs. Remove both registrations so upgrades, downgrades and
    scope changes never leave two Suite installers behind. }
  if not RemovePreviousSuite(HKCU) then
    Result := 'Could not remove the previous per-user Flux Suite installation.'
  else if not RemovePreviousSuite(HKLM64) then
    Result := 'Could not remove the previous system-wide Flux Suite installation.'
  else if not RemovePreviousSuite(HKLM) then
    Result := 'Could not remove the previous system-wide Flux Suite installation.';
end;

procedure InitializeWizard;
var
  IsUpdate: Boolean;
  WelcomeLogo: TBitmapImage;
  FinishedLogo: TBitmapImage;
begin
  IsUpdate := CompareText(ExpandConstant('{param:UPDATE|0}'), '1') = 0;

  WizardForm.Caption := 'Flux Suite';
  WizardForm.WizardBitmapImage.Visible := False;
  WizardForm.WizardBitmapImage2.Visible := False;

  ExtractTemporaryFile('flux-suite.ico');
  WelcomeLogo := TBitmapImage.Create(WizardForm.WelcomePage);
  WelcomeLogo.Parent := WizardForm.WelcomePage;
  WelcomeLogo.Left := ScaleX(56);
  WelcomeLogo.Top := ScaleY(46);
  WelcomeLogo.Width := ScaleX(58);
  WelcomeLogo.Height := ScaleY(58);
  WelcomeLogo.Stretch := True;
  WelcomeLogo.Center := True;
  InitializeBitmapImageFromIcon(
    WelcomeLogo, ExpandConstant('{tmp}\flux-suite.ico'),
    WizardForm.WelcomePage.Color, [32, 48, 64, 128, 256]);

  WizardForm.WelcomeLabel1.Font.Name := 'Segoe UI';
  WizardForm.WelcomeLabel1.Font.Size := 22;
  WizardForm.WelcomeLabel1.Left := ScaleX(136);
  WizardForm.WelcomeLabel1.Top := ScaleY(46);
  WizardForm.WelcomeLabel1.Width := WizardForm.WelcomePage.ClientWidth -
    WizardForm.WelcomeLabel1.Left - ScaleX(48);
  WizardForm.WelcomeLabel2.Font.Name := 'Segoe UI';
  WizardForm.WelcomeLabel2.Font.Size := 11;
  WizardForm.WelcomeLabel2.Left := ScaleX(136);
  WizardForm.WelcomeLabel2.Top := ScaleY(94);
  WizardForm.WelcomeLabel2.Width := WizardForm.WelcomePage.ClientWidth -
    WizardForm.WelcomeLabel2.Left - ScaleX(48);
  WizardForm.PageNameLabel.Font.Name := 'Segoe UI';
  WizardForm.PageDescriptionLabel.Font.Name := 'Segoe UI';

  FinishedLogo := TBitmapImage.Create(WizardForm.FinishedPage);
  FinishedLogo.Parent := WizardForm.FinishedPage;
  FinishedLogo.Left := ScaleX(56);
  FinishedLogo.Top := ScaleY(46);
  FinishedLogo.Width := ScaleX(58);
  FinishedLogo.Height := ScaleY(58);
  FinishedLogo.Stretch := True;
  FinishedLogo.Center := True;
  InitializeBitmapImageFromIcon(
    FinishedLogo, ExpandConstant('{tmp}\flux-suite.ico'),
    WizardForm.FinishedPage.Color, [32, 48, 64, 128, 256]);

  WizardForm.FinishedHeadingLabel.Font.Name := 'Segoe UI';
  WizardForm.FinishedHeadingLabel.Left := ScaleX(136);
  WizardForm.FinishedHeadingLabel.Top := ScaleY(46);
  WizardForm.FinishedHeadingLabel.Width := WizardForm.FinishedPage.ClientWidth -
    WizardForm.FinishedHeadingLabel.Left - ScaleX(48);
  WizardForm.FinishedLabel.Font.Name := 'Segoe UI';
  WizardForm.FinishedLabel.Font.Size := 11;
  WizardForm.FinishedLabel.Left := ScaleX(136);
  WizardForm.FinishedLabel.Top := ScaleY(94);
  WizardForm.FinishedLabel.Width := WizardForm.FinishedPage.ClientWidth -
    WizardForm.FinishedLabel.Left - ScaleX(48);
  WizardForm.NextButton.Font.Name := 'Segoe UI';
  WizardForm.BackButton.Font.Name := 'Segoe UI';
  WizardForm.CancelButton.Font.Name := 'Segoe UI';

  if IsUpdate then
  begin
    WizardForm.WelcomeLabel1.Caption := 'Update Flux Suite';
    WizardForm.WelcomeLabel2.Caption :=
      'A verified Flux Suite update is ready. Setup will update the installer and preserve your preferences.';
  end
  else
  begin
    WizardForm.WelcomeLabel1.Caption := 'Welcome to Flux Suite';
    WizardForm.WelcomeLabel2.Caption :=
      'Install the Flux creative workspace and keep every Flux application current from one place.';
  end;
end;
