from functools import lru_cache
import gymnasium as gym
import numpy as np

from . import Digger

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
        done = not self.digger.gamestep()
        reward = (newscore:=self.digger.getscore(0)) - self.lastscore
        assert reward >= 0
        self.lastscore = newscore
        return self.screenshot(), reward, done, False, {}

    def reset(self):
        self.digger.initgame()
        self.digger.startlevel()
        self.lastscore = self.digger.getscore(0)
        return self.screenshot(), {}

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
