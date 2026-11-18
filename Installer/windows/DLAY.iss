; DLAY - Windows installer (Inno Setup 6.3 or newer).
;
; Run from the repository root after a Release build:
;
;   iscc /DAppVersion=1.1.0 /DArtefacts=C:\path\to\build\DLAY_artefacts\Release /DOutputDir=C:\path\to\dist Installer\windows\DLAY.iss
;
; The result is DLAY-<version>-Windows-Setup.exe. It installs
;   - the VST3 plug-in to  C:\Program Files\Common Files\VST3\DLAY.vst3  (where every DAW looks)
;   - the standalone app to C:\Program Files\DLAY\  with a Start menu entry
; and registers an uninstaller under Settings > Apps.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef Artefacts
  #define Artefacts "..\..\build\DLAY_artefacts\Release"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

[Setup]
; This identifier is how Windows knows a newer DLAY replaces an older one. Never change it.
AppId={{6B1F4C2E-93A7-4D5B-8E21-D1A7C0FFEE21}
AppName=DLAY
AppVersion={#AppVersion}
AppVerName=DLAY {#AppVersion}
AppPublisher=DLAY Audio
AppPublisherURL=https://github.com/Vanago01/DLAY-VST
AppSupportURL=https://github.com/Vanago01/DLAY-VST
VersionInfoVersion={#AppVersion}
VersionInfoDescription=DLAY installer
MinVersion=10.0
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
; Nothing to choose: a VST3 plug-in has one correct place on Windows.
DefaultDirName={autopf}\DLAY
DisableDirPage=yes
DefaultGroupName=DLAY
DisableProgramGroupPage=yes
DisableWelcomePage=no
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
OutputDir={#OutputDir}
OutputBaseFilename=DLAY-{#AppVersion}-Windows-Setup
UninstallDisplayName=DLAY
UninstallDisplayIcon={app}\DLAY.exe

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
WelcomeLabel2=This will install DLAY {#AppVersion}: the VST3 plug-in for your DAW and the standalone app.%n%nClose your DAW before you continue.
FinishedLabelNoIcons=DLAY is installed.%n%nOpen your DAW and let it look for new plug-ins. In FL Studio: OPTIONS > Manage plugins > Find installed plugins.
FinishedLabel=DLAY is installed.%n%nOpen your DAW and let it look for new plug-ins. In FL Studio: OPTIONS > Manage plugins > Find installed plugins.

[InstallDelete]
; A copy made by hand from an earlier zip, or an older version, may hold files this one
; no longer has: start from an empty folder.
Type: filesandordirs; Name: "{commoncf64}\VST3\DLAY.vst3"

[Files]
Source: "{#Artefacts}\VST3\DLAY.vst3\*"; DestDir: "{commoncf64}\VST3\DLAY.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Artefacts}\Standalone\DLAY.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\..\INSTALL.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\DLAY"; Filename: "{app}\DLAY.exe"; Comment: "DLAY standalone"

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\DLAY.vst3"
