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

from ctypes import cdll
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
