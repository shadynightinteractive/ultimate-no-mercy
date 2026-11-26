"""Where each splice piece (data/extracted/keep: JR's cut names, lead-ins, Rumble openings) was cut from.
2026-11-14, commentary agent. Read-only outside this folder.

The splice bank (keep.json) says a lead-in's source line ("key": [game, clip]) but not where in it the cut is, and
says nothing about where a name was cut from. A rebuild from the player's disc needs both, so this finds them by
cross-correlating each piece (20 kHz) with the candidate source clips, decoded from the local caches:
  game 0: data/commentary_audio/wwe13/ogg/<wem id>.ogg (the pack's own encode: silence-trimmed, Vorbis q0)
  game 1: data/commentary_audio/lowm/mp3/NNNN.mp3 (Legends' clip as it is) and .../lowm/ogg/NNNN.ogg
A piece cut from a real line correlates near 1.0 with it (the encode's loss only); a piece made by the AI voice
matches nothing. Candidates for a name: the clips whose pack text (or Legends title) holds the name.
Out: splice_trace.tsv here:
  n  kind  from  game  text  best_score  source  offset_ms  gain  piece_ms  second_best
"""
import os, sys, json, re, glob, subprocess, collections
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lowprio  # noqa: F401
DEV = os.path.abspath(os.path.join(HERE, "..", ".."))   # (the source tree; data is linked in from the private folder)
W13 = os.path.join(DEV, "data", "commentary_audio", "wwe13", "ogg")
LOWM = os.path.join(DEV, "data", "commentary_audio", "lowm")
KEEP = os.path.join(DEV, "data", "extracted", "keep")
# keep.json names each file by the old sandbox's keep folder (/tmp/<sandbox>/keep/...): the rest is its place under KEEP
def keep_name(f): return re.sub(r"^/tmp/[^/]+/keep/", "", f)
R = 20000
_cache = {}

def dec(path):
    if path not in _cache:
        raw = subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-i", path, "-ac", "1", "-ar", str(R),
                              "-f", "s16le", "-"], capture_output=True).stdout
        _cache[path] = np.frombuffer(raw, dtype=np.int16).astype(np.float64) / 32768
        if len(_cache) > 600: _cache.pop(next(iter(_cache)))
    return _cache[path]

def id3_title(path):
    """the ID3v2 TIT2 text of an mp3 (Legends' name for the clip), or ''"""
    b = open(path, "rb").read(4096)
    i = b.find(b"TIT2")
    if b[:3] != b"ID3" or i < 0: return ""
    n = int.from_bytes(b[i + 4:i + 8], "big"); t = b[i + 10:i + 10 + n]
    enc, t = t[:1], t[1:]
    return (t.decode("utf-16", "ignore") if enc in (b"\x01", b"\x02") else t.decode("latin-1")).strip("\0 ")

def best_match(piece, src):
    """(normalised correlation, offset in samples, gain) of piece inside src"""
    n, m = len(piece), len(src)
    if m < n: src = np.concatenate([src, np.zeros(n - m)]); m = n
    L = 1 << int(np.ceil(np.log2(n + m)))
    c = np.fft.irfft(np.fft.rfft(src, L) * np.conj(np.fft.rfft(piece, L)), L)[:m - n + 1]
    e = np.concatenate([[0], np.cumsum(src ** 2)])
    win = np.sqrt(np.maximum(e[n:] - e[:-n], 1e-12))
    pn = np.sqrt(np.sum(piece ** 2)) + 1e-12
    s = c / (win * pn)
    k = int(np.argmax(s))
    gain = float(np.dot(src[k:k + n], piece) / (np.dot(src[k:k + n], src[k:k + n]) + 1e-12))
    return float(s[k]), k, gain

def norm(t): return re.sub(r"[^a-z0-9 ]", "", t.lower())

def main():
    keep = json.load(open(os.path.join(KEEP, "keep.json"), encoding="utf-8"))
    # pack texts by source clip (from map_clips.py's clipmap.tsv)
    cm = [l.split("\t") for l in open(os.path.join(HERE, "clipmap.tsv"), encoding="utf-8").read().splitlines()[1:]]
    texts = collections.defaultdict(list)     # (game, id) -> texts
    lines = [l.split("\t") for l in open(os.path.join(HERE, "pack_lines.tsv"), encoding="utf-8").read().splitlines()]
    clip_key = {int(r[0]): (int(r[1]), int(r[2])) for r in cm if r[1] in ("0", "1")}
    for f in lines:
        k = clip_key.get(int(f[0]))
        if k: texts[k].append(f[10])
    titles = {int(os.path.basename(p)[:-4]): id3_title(p) for p in glob.glob(os.path.join(LOWM, "mp3", "*.mp3"))}
    out = []
    for n, x in enumerate(keep):
        f = keep_name(x["file"])
        piece = dec(os.path.join(KEEP, f))
        game = x.get("game")
        cands = []
        if x["kind"] == "lead":
            cands = [tuple(x["key"])]
        else:
            words = [w for w in norm(x["text"]).split() if len(w) >= 3 and w not in ("the", "and")]
            # (names whisper spells its own way: try the longest word's first 4 letters)
            pat = max(words, key=len)[:4] if words else norm(x["text"])[:4]
            for k, ts in texts.items():
                if any(pat in norm(t) for t in ts): cands.append(k)
            for k, t in titles.items():
                if pat in norm(t.replace("_", " ")) and (1, k) not in cands: cands.append((1, k))
        res = []
        for g, i in cands:
            paths = [os.path.join(W13, "%d.ogg" % i)] if g == 0 else [os.path.join(LOWM, "mp3", "%04d.mp3" % i),
                                                                      os.path.join(LOWM, "ogg", "%04d.ogg" % i)]
            for p in paths:
                if not os.path.exists(p): continue
                s, off, gain = best_match(piece, dec(p))
                res.append((s, "%d:%d:%s" % (g, i, os.path.basename(p)[-3:]), off, gain))
        res.sort(reverse=True)
        b = res[0] if res else (0.0, "-", 0, 0.0)
        s2 = "%.3f %s" % res[1][:2] if len(res) > 1 else "-"
        out.append("%d\t%s\t%s\t%s\t%s\t%.4f\t%s\t%.1f\t%.3f\t%d\t%s\t%d" % (
            n, x["kind"], x.get("from", "-"), game, x["text"], b[0], b[1], b[2] * 1000 / R, b[3], len(piece) * 1000 / R,
            s2, len(cands)))
        print(out[-1], flush=True)
    open(os.path.join(HERE, "splice_trace.tsv"), "w", encoding="utf-8", newline="\n").write(
        "n\tkind\tfrom\tgame\ttext\tscore\tsource\toffset_ms\tgain\tpiece_ms\tsecond\tcands\n" + "\n".join(out) + "\n")

if __name__ == "__main__":
    main()
