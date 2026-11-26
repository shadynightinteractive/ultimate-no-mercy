# (names083, 2026-11-14; from namesprep's paths.py) Where the names tools find things, worked out from where this file is
# (tools/names/) and from environment variables, so they run from any folder on any system and no path is written in them.
#   D      the project (Development/)
#   L      the lift tools whose lift.py, names.json, candidates.json and fields.json are read: NAMES_LIFT, else tools/lift/.
#          Read only -- the names tools never write there; an agent points NAMES_LIFT at its own tools copy.
#   B      your work folder: NAMES_WORK (required, e.g. scratch/agents/<you>/): the changes file and the tools' outputs
#   T      B/tools/: the tools' outputs (*_edits.json, graph.json, fpk.pickle), where names5 kept them beside its changes
#   BASE   the names.json the changes go on top of: NAMES_BASE, else L/names.json
#   CANDS  the converted functions: L/candidates.json
#   CH     the changes file: NAMES_CHANGES, else B/changes.json
import os

CODE = os.path.dirname(os.path.abspath(__file__)).replace('\\', '/') + '/'
D = os.path.dirname(os.path.dirname(CODE.rstrip('/'))).replace('\\', '/') + '/'
L = (os.environ.get('NAMES_LIFT') or D + 'tools/lift').replace('\\', '/').rstrip('/') + '/'
if not os.environ.get('NAMES_WORK'):
    raise SystemExit('names tools: set NAMES_WORK to your work folder (e.g. scratch/agents/<you>); the outputs go there, never here')
B = os.path.abspath(os.environ['NAMES_WORK']).replace('\\', '/') + '/'
T = B + 'tools/'
os.makedirs(T, exist_ok=True)
BASE = os.environ.get('NAMES_BASE') or L + 'names.json'
CANDS = L + 'candidates.json'
CH = os.environ.get('NAMES_CHANGES') or B + 'changes.json'
