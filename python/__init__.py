# Copyright (c) 2024 Sippy Software, Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from ctypes import cdll, c_bool, c_ubyte, c_int, POINTER, Structure, c_int32
import os, site, sysconfig

from .env import DIGGER_MOD_NAME

_esuf = sysconfig.get_config_var('EXT_SUFFIX')
if not _esuf:
    _esuf = '.so'
try:
    import pathlib
    _ROOT = str(pathlib.Path(__file__).parent.absolute())
except ImportError:
    _ROOT = os.path.abspath(os.path.dirname(__file__))
#print('ROOT: ' + str(_ROOT))
modloc = site.getsitepackages()
modloc.insert(0, os.path.join(_ROOT, ".."))
for p in modloc:
   try:
       #print("Trying %s" % os.path.join(p, DIGGER_MOD_NAME + _esuf))
       _digger = cdll.LoadLibrary(os.path.join(p, DIGGER_MOD_NAME + _esuf))
   except:
       continue
   break
#else:
#   raise ImportError("Cannot find %s" % DIGGER_MOD_NAME + _esuf)

class DiggerControls(Structure):
    _fields_ = [
        ("leftpressed", c_bool),
        ("rightpressed", c_bool),
        ("uppressed", c_bool),
        ("downpressed", c_bool),
        ("f1pressed", c_bool),
        ("left2pressed", c_bool),
        ("right2pressed", c_bool),
        ("up2pressed", c_bool),
        ("down2pressed", c_bool),
        ("f12pressed", c_bool)
    ]

class Digger:
    def __init__(self):
        self.gamestep = _digger.gamestep
        self.gamestep.restype = c_bool
        self.maininit = _digger.maininit
        self.maininit.restype = None
        self.initgame = _digger.initgame
        self.startlevel = _digger.startlevel
        self.startlevel.restype = None
        self.getscreen = _digger.getscreen
        self.getscreen.argtypes = [POINTER(c_ubyte), c_int]
        self.getscreen.restype = None
        self.getscore = _digger.gettscore
        self.getscore.argtypes = [c_int]
        self.getscore.restype = c_int32
        self.digger_controls = DiggerControls.in_dll(_digger, "digger_controls")
        self.soundflag = c_bool.in_dll(_digger, 'soundflag')
        self.musicflag = c_bool.in_dll(_digger, 'musicflag')

    def game(self, steps=None):
        self.maininit()
        self.initgame()
        self.startlevel()
        while (steps is None or steps > 0) and self.gamestep(): steps -= 1
        return steps
    
    def screenshot(self):
        buffer_size = 640 * 400
        buffer = (c_ubyte * buffer_size)()
        self.getscreen(buffer, buffer_size)
        return buffer

