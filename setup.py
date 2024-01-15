#!/usr/bin/env python

import os, subprocess

from distutils.core import setup
from distutils.core import Extension

from python.env import DIGGER_MOD_NAME

def get_SDL2_flags(args):
    try: cflags = subprocess.check_output(["pkg-config", "sdl2", args], universal_newlines=True)
    except subprocess.CalledProcessError as ex: raise RuntimeError("pkg-config failed to find SDL2 flags") from ex
    return cflags.strip().split()

directory = os.path.dirname(os.path.realpath(__file__))
dg_srcs = ['digger.c', 'drawing.c', 'sprite.c', 'scores.c', 'record.c', 'sound.c', 'ini.c', 'input.c',
           'monster.c', 'bags.c', 'alpha.c', 'vgagrafx.c', 'digger_math.c', 'monster_obj.c', 'digger_obj.c',
           'bullet_obj.c', 'cgagrafx.c', 'keyboard.c', 'soundgen.c', 'spinlock.c', 'game.c', 'fbsd_snd.c',
           'main.c', 'fbsd_sup.c', 'sdl_vid.c', 'icon.c', 'title_gz.c', 'python/python_timer.c',
           'python/python_sound.c', 'python/python_kbd.c']

extra_compile_args = ['-DLINUX', '-D_SDL', '-D_PYTHON', f'-I{directory}', '--std=c11', '-Wno-variadic-macros', '-Wall', '-pedantic', '-flto']
extra_compile_args.extend(get_SDL2_flags('--cflags'))
extra_link_args = ['-flto', '-lm']
extra_link_args.extend(get_SDL2_flags('--libs'))
debug_opts = (('-g3', '-O0', '-DDIGGER_DEBUG'))
nodebug_opts = (('-march=native', '-O3'))
if False:
    extra_compile_args.extend(debug_opts)
    extra_link_args.extend(debug_opts)
else:
    extra_compile_args.extend(nodebug_opts)
    extra_link_args.extend(nodebug_opts)

module1 = Extension(DIGGER_MOD_NAME, sources = dg_srcs, \
    extra_link_args = extra_link_args, \
    extra_compile_args = extra_compile_args)

extra_link_args.append('-Wl,--version-script=python/Symbol.map')

def get_ex_mod():
    if 'NO_PY_EXT' in os.environ:
        return None
    return [module1]

with open("README.md", "r") as fh:
    long_description = fh.read()

kwargs = {'name':'Digger',
      'version':'1.0',
      'description':'Classic game of Digger',
      'long_description': long_description,
      'long_description_content_type': "text/markdown",
      'author':'Maksym Sobolyev',
      'author_email':'sobomax@gmail.com',
      'url':'https://github.com/sobomax/digger.git',
      'packages':['digger',],
      'package_dir':{'digger':'python'},
      'ext_modules': get_ex_mod(),
      'classifiers': [
            'License :: OSI Approved :: BSD License',
            'Operating System :: POSIX',
            'Programming Language :: C',
            'Programming Language :: Python'
      ]
     }

if __name__ == '__main__':
    setup(**kwargs)
