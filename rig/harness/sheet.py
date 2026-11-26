import sys
from PIL import Image
out, files = sys.argv[1], sys.argv[2:]
ims = [Image.open(f).resize((320, 120)) for f in files]
cols = 3; rows = (len(ims) + cols - 1) // cols
S = Image.new("RGB", (320 * cols, 120 * rows))
for i, im in enumerate(ims): S.paste(im, ((i % cols) * 320, (i // cols) * 120))
S.save(out)
