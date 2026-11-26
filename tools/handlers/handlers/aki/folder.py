"""A standard wrestler folder (2026-11-12, handlers): what every handler writes, in rosterids' format (the folder's name is
his string ID, collections are free-form folders above it, "id" pins it), with two additions this prototype proposes:
"model" (his own low-detail model: aki/model.py) and "source" (where it came from, so a handler can be run again).

    <out>/<collection>/<string id>/wrestler.json, photo.bin, model/...

legacy=True writes the live game's older keys instead ("id" and "based_on" as game numbers, "0x0340"), for a test in
today's exe, which doesn't read string IDs yet (rosterids' change isn't merged)."""
import json, os

def write(out, collection, sid, w, photo=None):
    """w: the wrestler.json fields (already in the chosen ID style). -> the folder"""
    d = os.path.join(out, collection, sid)
    os.makedirs(d, exist_ok=True)
    if photo is not None:
        assert len(photo) == 32 + 2048
        open(os.path.join(d, "photo.bin"), "wb").write(photo)
    with open(os.path.join(d, "wrestler.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(w, f, indent=1)
    return d
