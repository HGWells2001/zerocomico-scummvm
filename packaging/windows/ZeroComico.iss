#ifndef SourceDir
  #define SourceDir "..\\..\\dist"
#endif
#ifndef OutputDir
  #define OutputDir "..\\..\\installer-out"
#endif

[Setup]
AppId={{A997D517-DC3B-4FD3-A326-8A197D5565BC}
AppName=Zero Comico - ScummVM
AppVersion=0.1-dev
AppPublisher=Zero Comico ScummVM project
DefaultDirName={localappdata}\\Zero Comico ScummVM
DefaultGroupName=Zero Comico - ScummVM
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir={#OutputDir}
OutputBaseFilename=ZeroComico-ScummVM-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\\scummvm.exe

[Files]
Source: "{#SourceDir}\\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\\Zero Comico - ScummVM"; Filename: "{app}\\scummvm.exe"
Name: "{autoprograms}\\Configura Zero Comico"; Filename: "{app}\\Configura-Zero-Comico.cmd"; WorkingDir: "{app}"
Name: "{autodesktop}\\Zero Comico - ScummVM"; Filename: "{app}\\AVVIA-ZERO-COMICO.cmd"; WorkingDir: "{app}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Crea collegamento sul desktop"; GroupDescription: "Collegamenti:"; Flags: unchecked

[Run]
Filename: "{app}\\Configura-Zero-Comico.cmd"; Description: "Configura la cartella del gioco originale e avvia Zero Comico"; Flags: postinstall nowait skipifsilent
