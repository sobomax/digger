from ctypes import c_bool, c_ubyte, c_int, POINTER, Structure, c_int16, c_int32

from . import _digger

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

class DiggerState(Structure):
    _fields_ = [
        ("h", c_int16),
        ("v", c_int16),
        ("rx", c_int16),
        ("ry", c_int16),
        ("mdir", c_int16),
        ("bagtime", c_int16),
        ("rechargetime", c_int16),
        ("deathstage", c_int16),
        ("deathbag", c_int16),
        ("deathani", c_int16),
        ("deathtime", c_int16),
        ("emocttime", c_int16),
        ("emn", c_int16),
        ("msc", c_int16),
        ("lives", c_int16),
        ("ivt", c_int16),
        ("notfiring", c_bool),
        ("firepressed", c_bool),
        ("dead", c_bool),
        ("levdone", c_bool),
        ("invin", c_bool),
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
        self._getdigdat = _digger.getdigdat
        self._getdigdat.argtypes = [c_int]
        self._getdigdat.restype = POINTER(DiggerState)
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

    def showscreen(self):
        from PIL import Image
        import numpy as np
        s = self.screenshot()
        image_data = np.frombuffer(s, dtype=np.uint8).reshape((400, 640))
        image = Image.fromarray(image_data, 'P')
        image.putpalette(generate_vga_palette())
        image.show()
    
    def getdigdat(self, player):
        return self._getdigdat(player).contents
