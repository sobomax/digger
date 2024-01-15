from PIL import Image
import numpy as np
import gymnasium as gym

from digger import Digger

def generate_vga_palette():
    palette = []
    for i in range(256):
        r = (i & 0b0011) * 85
        g = ((i & 0b1100) >> 2) * 85
        b = ((i & 0b110000) >> 4) * 85
        palette.extend([r, g, b])
    return palette

def showscreen(d):
    s = d.screenshot()
    image_data = np.frombuffer(s, dtype=np.uint8).reshape((400, 640))
    image = Image.fromarray(image_data, 'P')
    image.putpalette(generate_vga_palette())
    image.show()

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

    def render(self):
        showscreen(self.digger)

    def screenshot(self):
        return np.frombuffer(self.digger.screenshot(), dtype=np.uint8).reshape((400, 640))

    def close(self):
        pass

    def seed(self, seed=None):
        pass

if __name__ == '__main__':
    d= Digger()
    r = d.game(1)
    assert r == 0
    while True:
        if not d.gamestep(): break
    showscreen(d)
    env = DiggerGym()
    for _ in range(100000):
        action = env.action_space.sample()  # agent policy that uses the observation and info
        observation, reward, terminated, truncated, info = env.step(action)

        if terminated or truncated:
            observation, info = env.reset()

    env.close()
