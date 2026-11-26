# handlers (2026-11-12): importers that read a user's own AKI-game ROM (or mod) and write standard wrestler folders.
# (2026-11-13, final082: moved into the project as Development\tools\handlers -- the aki core and the revenge, wm2000,
#  nomercy, baddblood and customs handlers, with the scripts beside this package: aki_import.py, nm_import.py,
#  imports2.py, movelib.py and the checks. Paths are found from here: DEV below.)
import os


def _development():
    """The Development folder: the first folder above this package that has nmpaths.py, the project's path helper (so the
    tools work wherever they sit in it: tools\\handlers in the project, an agent's copy in scratch). 2026-11-13, final082"""
    d = os.path.dirname(os.path.abspath(__file__))
    while not os.path.exists(os.path.join(d, 'nmpaths.py')):
        up = os.path.dirname(d)
        if up == d:
            raise SystemExit('handlers: no nmpaths.py above %s -- run the tools from inside the Development folder' % __file__)
        d = up
    return d


DEV = _development()
# (The source-game rig's captures -- Badd Blood's models, theme bank, Superstar Select -- that the baddblood handler and
#  imports2.py read: rig\bb.py / capture.py write them to <RIG>\cap. NM_HANDLERS_RIG names another folder.)
RIG = os.environ.get('NM_HANDLERS_RIG') or os.path.join(DEV, 'data', 'handlers_rig')
