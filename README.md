[![Build Status (Linux)](https://github.com/sobomax/digger/actions/workflows/linux.yml/badge.svg?branch=master)](https://github.com/sobomax/digger/actions/workflows/linux.yml?query=branch%3Amaster++)
[![Build Status (Windows)](https://ci.appveyor.com/api/projects/status/s3nmvbv7xnt9uuyh/branch/master?svg=true)](https://ci.appveyor.com/project/sobomax/digger/branch/master)
[![Coverage Status](https://coveralls.io/repos/github/sobomax/digger/badge.svg?branch=master)](https://coveralls.io/github/sobomax/digger?branch=master)
[![Coverity](https://scan.coverity.com/projects/17679/badge.svg)](https://scan.coverity.com/projects/sobomax-digger)
[![Download ZIP](https://img.shields.io/badge/Windows-Download_ZIP-orange.svg)](https://ci.appveyor.com/api/projects/sobomax/digger/artifacts/digger-win64.zip?branch=master)
[![Download Installer](https://img.shields.io/badge/Windows-Download_Installer-orange.svg)](https://ci.appveyor.com/api/projects/sobomax/digger/artifacts/DiggerRemastered-Setup.exe?branch=master)

# This is Digger Reloaded, aka UNIX/Linux Digger, however it also works on Windows.

Digger was originally created by Windmill Software in 1983 and released as a
copy-protected, bootable 5.25" floppy disk for the IBM PC. As it requires a
genuine CGA card, it didn't work on modern PCs.

In 1998, Andrew Jenner <aj@digger.org>, created Digger Remastered, which runs
on all PCs with CGA or better and plays just like the original. See
http://digger.org for more information about his remake, the history of the
game as well as the history of Windmill Software and their other games.

Later on, Maksym Sobolyev <sobomax@gmail.com>, ported Adnrew's code to run
on Linux/FreeBSD/Windows using SDL library, or on FreeBSD console using
native "VGL" VESA graphics interface. That version is now being actively
developed to move Digger into XXI century.

Furthermore, Michael Knigge <michael.knigge@gmx.de>, did some minor
enhancements, cleaned up the code a little bit, fixed some minor bugs, gave
the "redefine keyboard" dialog a new look and feel and provided an Installer
for Microsoft Windows along with the Appveyor CI scripts.

Very recently, the WebAssembly version has been added as well, allowing
Digger to run directly in your browser.

# Try It Out

[![WebAssembly](https://digger-build-artefacts.s3.amazonaws.com/digger_wasm.png)](https://digger-build-artefacts.s3.amazonaws.com/digger.html)

# Network Play

Two-player network play is enabled with the `/N:` option. There are two
supported modes:

1. Peer-to-peer:
   one side acts as a small local SIP registrar and listens on UDP `5060`,
   while the other side registers to it directly.

   Examples:

   - Host/listener: `/N:alice-bob`
   - Peer connecting to host `192.168.50.10`: `/N:bob@192.168.50.10-alice`

2. Via a SIP server:
   both sides use a normal SIP server, register there, and place the game
   call through that server.

   Examples:

   - Alice via SIP server: `/N:alice[:password]@sip.example.com[:5060]-bob`
   - Bob via SIP server: `/N:bob[:password]@sip.example.com[:5060]-alice`

In both modes, the value before `-` is the local SIP user and the value after
`-` is the peer SIP user. In peer-to-peer mode the host side omits `@server`
and binds to `*:5060`; in SIP-server mode `@server[:port]` is required.

# Future Plans

Some plans for the future releases include:

- 2.0, aka "Digger Reset"
  - [x] SDL 2.0 port
  - [x] fullscreen mode for Windows
  - [x] toggle fullscreen and window mode with Alt-Enter
  - [x] installer for Windows
  - [ ] revised redefine keyboard
  - [ ] make CGA graphics available again (for nostalgia)
  - [ ] further code cleanup (in-progress)
      - [x] move logic into high-level objects
      - [x] remove obsolete command line options
      - [x] 64-bit Windows build
      - [ ] remove obsolete DOS functions (i. e. function s0setupsound() and
            so on)
      - [x] remove ARM specific code (SDL 2.0 is not available on ARM, so the
            ARM specific code is useless)
      - [ ] remove DIGGER_VERSION and give Digger a regular version number
  - [ ] headless (VNC?) mode
  - [x] network play for 2 players
  - [ ] screen update framerate that is independent of game perceived speed,
        "sub-pixel" updates

- 3.0, aka "Digger Reloaded"

  - [ ] bigger (open?) game world
  - [ ] MMOG mode
  - [ ] deathmatch mode
  - [ ] new capabilities to the characters (i.e. superpowers), and interactions
        between them (i.e. gradual damage)
  - [ ] few types of diggers: scout, soldier, sniper etc.
  - [ ] monster AI 2.0?
  - [ ] digger AI to make teamplay/deathmatch interesting even in single-player mode
  - [ ] teamplay

The source code will be free and open at all times. Digger Remastered is
source code has been contributed by different people and licensed under the
terms of several licenses including Public Domain, Beer-Ware, 2-clause BSD
and GNU General Public License version 2. Please refer to a specific source
file as well as source code history to find out more.
