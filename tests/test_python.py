from digger import Digger
from digger.DiggerGym import DiggerGym

if __name__ == '__main__':
    #d= Digger()
    #r = d.game(1)
    #assert r == 0
    #while True:
    #    if not d.gamestep(): break
    #d.showscreen()
    import numpy as np
    #rgb = np.frombuffer(d.screenshot(), dtype=np.uint8).reshape((400, 640))
    #print(f'{rgb.shape=} {np.unique(rgb)=}')
    env = DiggerGym()
    env.reset()
    for _ in range(100000):
        action = env.action_space.sample()  # agent policy that uses the observation and info
        observation, reward, terminated, truncated, info = env.step(action)

        if terminated or truncated:
            break
            observation, info = env.reset()

    print(f'{env.screenshot().shape=} {np.unique(env.screenshot())=}')
    env.close()
