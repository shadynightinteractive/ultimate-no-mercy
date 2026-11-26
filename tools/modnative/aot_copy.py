#!/usr/bin/env python3
"""Stage 1's translation of THIS folder's mod build (modnative5, 2026-11-13): tools/aot/aot.py's own generate(), run on
w/mod/build/mod_six/nomercy_arcade.nrm, with everything it writes kept in w/: its copy of aot_tool.exe (w/aot/build), the
files it takes out of the .nrm (w/aot/build/nrm) and the translation itself (w/mod_aot). The live tools/aot is only read.
With the three AkiMods as aot.py has them (their hooks reach the arcade mod's copies).

    python aot_copy.py [<.nrm>]
"""
import os, shutil, sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEV = os.path.abspath(os.path.join(HERE, '..', '..'))
W = os.path.join(HERE, 'w')
sys.path.insert(0, os.path.join(DEV, 'tools', 'aot'))
import aot   # (only its functions: its import writes nothing)

nrm = sys.argv[1] if len(sys.argv) > 1 else os.path.join(W, 'mod', 'build', 'mod_six', 'nomercy_arcade.nrm')
aot.HERE = os.path.join(W, 'aot')
aot.TOOL = os.path.join(aot.HERE, 'build', 'aot_tool.exe')
if not os.path.exists(aot.TOOL) or os.path.getmtime(aot.TOOL) < os.path.getmtime(os.path.join(DEV, 'tools', 'aot', 'build', 'aot_tool.exe')):
    os.makedirs(os.path.dirname(aot.TOOL), exist_ok=True)
    shutil.copy2(os.path.join(DEV, 'tools', 'aot', 'build', 'aot_tool.exe'), aot.TOOL)
aot.build_tool = lambda: aot.TOOL      # (the project's tool as built, copied: never rebuilt here)
aot.OUT = os.path.join(W, 'mod_aot')
aot.MODS = [nrm] + aot.MODS[1:]     # (and the three AkiMods the game ships with, read where the game has them: their hooks
                                    #  are in the arcade mod's copies too, as in the exe)
os.makedirs(aot.OUT, exist_ok=True)
aot.generate()
print('hash %s -> %s' % (aot.mod_hash(nrm), aot.OUT))
