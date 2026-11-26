"""A QA run's screenshots as one 3 x 3 sheet (nine spread over the run, black ones skipped) for a quick look.
    python nine.py RUN_DIR OUT.png"""
import glob, os, re, sys
from PIL import Image, ImageDraw

run, out = sys.argv[1], sys.argv[2]
shots = [f for f in glob.glob(os.path.join(run, 'shots', '*.png')) if re.match(r'^[0-9.]+s\.png$', os.path.basename(f))]
shots.sort(key=lambda f: float(os.path.basename(f)[:-5]))
lit = [f for f in shots if Image.open(f).convert('L').getextrema()[1] > 10]
pick = lit[::max(1, len(lit) // 9)][:9] if lit else []
sheet = Image.new('RGB', (3 * 400, 3 * 300))
for i, f in enumerate(pick):
    im = Image.open(f).convert('RGB').resize((400, 300))
    ImageDraw.Draw(im).text((4, 4), os.path.basename(f), fill=(255, 255, 0))
    sheet.paste(im, ((i % 3) * 400, (i // 3) * 300))
sheet.save(out)
print(len(shots), 'shots,', len(lit), 'not black ->', out)
