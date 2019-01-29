[Setup]
AppName=Digger Remastered
AppVerName=Digger Remastered {#GetEnv("APPVEYOR_BUILD_VERSION")}
AppVersion={#GetEnv("APPVEYOR_BUILD_VERSION")}
AppContact=sobomax@gmail.com
AppPublisher=Maksym Sobolyev
AppPublisherURL=https://github.com/sobomax/digger

DefaultDirName={pf}\Digger
DefaultGroupName=Digger

OutputBaseFilename=DiggerRemastered-Setup
OutputDir=.

[Icons]
Name: "{group}\Digger";                            Filename: "{app}\digger.exe"; WorkingDir: "{app}"
Name: "{group}\Digger (Two-Player Simultaneous)";  Filename: "{app}\digger.exe"; WorkingDir: "{app}"; Parameters: "/2"
Name: "{group}\Digger (Gauntlet Mode)";            Filename: "{app}\digger.exe"; WorkingDir: "{app}"; Parameters: "/G"
Name: "{group}\Redefine Keyboard (One Player)";    Filename: "{app}\digger.exe"; WorkingDir: "{app}"; Parameters: "/K"
Name: "{group}\Redefine Keyboard (Two Players)";   Filename: "{app}\digger.exe"; WorkingDir: "{app}"; Parameters: "/2 /K"
Name: "{group}\Read Me";                           Filename: "{app}\README.txt"
Name: "{group}\Uninstall";                         Filename: "{uninstallexe}"

[Files]
Source: "README.txt";     DestDir: "{app}"; DestName: "README.txt"; Flags: isreadme
Source: "LICENSE";        DestDir: "{app}"

Source: "..\..\digger.exe";     DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\zlib1.dll";      DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\SDL2.dll";       DestDir: "{app}"; Flags: ignoreversion

Source: "..\..\digger.log";     DestDir: "{app}"; Permissions: everyone-modify
Source: "..\..\digger.sco";     DestDir: "{app}"; Permissions: everyone-modify; Flags: onlyifdoesntexist
Source: "..\..\digger.ini";     DestDir: "{app}"; Permissions: everyone-modify; Flags: onlyifdoesntexist

[Run]
// User selected... these files are shown for launch after everything is done
Filename: "{app}\README.txt"; Description: "View the README file"; Flags: postinstall shellexec skipifsilent
Filename: "{app}\digger.exe"; Description: "Run Digger"; Flags: postinstall nowait skipifsilent unchecked
