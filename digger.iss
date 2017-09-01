[Setup]
AppName=Digger Remastered
AppVerName=Digger Remastered {#GetEnv("APPVEYOR_BUILD_VERSION")}
AppVersion={#GetEnv("APPVEYOR_BUILD_VERSION")}
AppContact=michael.knigge@gmx.de
AppPublisher=Michael Knigge
AppPublisherURL=https://github.com/michaelknigge/digger

DefaultDirName={pf}\Digger Remastered
DefaultGroupName=Digger Remastered

OutputBaseFilename=DiggerRemastered-Setup
OutputDir=.

[Icons]
Name: "{group}\Digger Remastered (Window)";      Filename: "{app}\digger.exe"; WorkingDir: "{app}"
Name: "{group}\Digger Remastered (Fullscreen)";  Filename: "{app}\digger.exe"; WorkingDir: "{app}"; Parameters: "/F"
Name: "{group}\Uninstall";                       Filename: "{uninstallexe}"

[Files]
Source: "README.txt";     DestDir: "{app}"; DestName: "README.txt"; Flags: isreadme
Source: "LICENSE";        DestDir: "{app}"

Source: "digger.exe";     DestDir: "{app}"; Flags: ignoreversion
Source: "zlib1.dll";      DestDir: "{app}"; Flags: ignoreversion
Source: "SDL2.dll";       DestDir: "{app}"; Flags: ignoreversion

Source: "digger.sco";     DestDir: "{app}"; Permissions: everyone-modify; Flags: onlyifdoesntexist
Source: "digger.ini";     DestDir: "{app}"; Permissions: everyone-modify; Flags: onlyifdoesntexist
