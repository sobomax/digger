[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![Build Status](https://travis-ci.org/michaelknigge/digger.svg?branch=master)](https://travis-ci.org/michaelknigge/digger)
[![Build status](https://ci.appveyor.com/api/projects/status/j89k9v2qrxqp6mgt/branch/master?svg=true)](https://ci.appveyor.com/project/michaelknigge/digger/branch/master)
[![Download ZIP](https://img.shields.io/badge/Windows-Download_ZIP-orange.svg)](https://ci.appveyor.com/api/projects/michaelknigge/digger/artifacts/digger-win32.zip?branch=master)
[![Download Installer](https://img.shields.io/badge/Windows-Download_Installer-orange.svg)](https://ci.appveyor.com/api/projects/michaelknigge/digger/artifacts/DiggerRemastered-Setup.exe?branch=master)

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
      - [ ] remove ARM specific code (SDL 2.0 is not available on ARM, so the
            ARM specific code is useless)
      - [ ] remove DIGGER_VERSION and give Digger a regular version number
  - [ ] headless (VNC?) mode
  - [ ] network play for 2 players
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
