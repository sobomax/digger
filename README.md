[![Build Status](https://travis-ci.org/sobomax/digger.svg?branch=master)](https://travis-ci.org/sobomax/digger)

# This is Digger Reloaded, aka UNIX/Linux Digger, however it also works on Windows.

Digger was originally created by Windmill Software in 1983 and released as a
copy-protected, bootable 5.25" floppy disk for the IBM PC. As it requires a
genuine CGA card, it didn't work on modern PCs.

In 1998, Andrew Jenner <aj@digger.org>, created Digger Remastered, which runs
on all PCs with CGA or better and plays just like the original. 

Later on, Maksym Sobolyev <sobomax@gmail.com>, ported Adnrew's code to run
on Linux/FreeBSD/Windows using SDL library, or on FreeBSD console using
native "VGL" VESA graphics interface. That version is now being actively
developed to move Digger into XXI century.

Some plans for the future releases include:

- 2.0, aka "Digger Reset"

 o SDL 2.0 port;
 o further code cleanup (i.e. elimination of the d3adc0d3: WIN16, ETC), more
   logic moved into high-level objects;
 o headless (VNC?) mode;
 o network play for 2 players;
 o screen update framerate that is independent of game perceived speed,
   "sub-pixel" updates.

- 3.0, aka "Digger Reloaded"

 o bigger (open?) game world;
 o MMOG mode;
 o deathmatch mode;
 o new capabilities to the characters (i.e. superpowers), and interactions between
   them (i.e. gradual damage);
 o few types of diggers: scout, soldier, sniper etc.
 o monster AI 2.0?
 o digger AI to make teamplay/deathmatch interesting even in single-player mode.
 o teamplay.

We plan to keep the source code free and open at all times.
