from functools import lru_cache
import gymnasium as gym
import numpy as np

from .Digger import Digger

class DiggerGym(gym.Env):
    lastscore:int
    digger:Digger
    def __init__(self):
        super().__init__()
        self.digger = Digger()
        self.digger.soundflag = self.digger.musicflag = False
        self.digger.maininit()
        self.action_space = gym.spaces.Discrete(5)
        self.observation_space = gym.spaces.Box(low=0, high=255, shape=(400, 640), dtype=np.uint8)

    def step(self, action):
        self.digger.digger_controls.leftpressed = (action == 0)
        self.digger.digger_controls.rightpressed = (action == 1)
        self.digger.digger_controls.uppressed = (action == 2)
        self.digger.digger_controls.downpressed = (action == 3)
        self.digger.digger_controls.f1pressed = (action == 4)
        if action not in range(5): raise ValueError(f"Invalid action: {action}")
        dead = self.digger.getdigdat(0).deathstage > 1
        done = not self.digger.gamestep() or dead
        reward = (newscore:=self.digger.getscore(0)) - self.lastscore
        assert reward >= 0
        self.lastscore = newscore
        return self.getscreenrgb(), reward, done, dead, {}

    def reset(self):
        self.digger.initgame()
        self.digger.startlevel()
        self.lastscore = self.digger.getscore(0)
        return self.getscreenrgb(), {}

    def getscreenrgb(self):
        s = self.digger.screenshot()
        @lru_cache(maxsize=1)
        def generate_vga_palette():
            palette = np.zeros((256, 3), dtype=np.uint8)
            for i in range(256):
                r = (i & 0b0011) * 85
                g = ((i & 0b1100) >> 2) * 85
                b = ((i & 0b110000) >> 4) * 85
                palette[i] = [r, g, b]
            return palette
        image_data = np.frombuffer(s, dtype=np.uint8).reshape((400, 640))
        palette = generate_vga_palette()
        return palette[image_data]

    def screenshot(self):
        s = self.digger.screenshot()
        s = np.frombuffer(s, dtype=np.uint8).reshape((400, 640))
        s = s.astype(np.float32) / 15.0
        return s

    def render(self):
        self.digger.showscreen()

    def close(self):
        pass

    def seed(self, seed=None):
        pass
