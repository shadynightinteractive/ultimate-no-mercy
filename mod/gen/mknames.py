#!/usr/bin/env python3
"""JR's name bank and lead-ins, cut from his own lines (word timings: whisper.cpp -ml 1 -sow, /tmp/devbox-0/names/ts).

  names : a Superstar's name as JR says it ("Kurt Angle", "Michaels", "the Olympian") -- cut where he says it with a
          pause after (a line that ends on it, or a comma / full stop after it)
  leads : the start of a line that ends "... by <name>" / "here comes <name>" / "down goes <name>" -- "What an elbow by",
          to be followed by whoever did it (the director plays lead + name)
Each cut is checked by transcribing it again: a name must come back as that name, a lead-in as its words.
Output: /tmp/devbox-0/names/cut/*.wav (20 kHz mono), /tmp/devbox-0/names/splices.json
"""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import json, os, re, glob, subprocess, wave, sys
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from names import NAMES
N = nmp("/tmp/devbox-0/names/")
LOWM_AF = open(nmp("/tmp/devbox-0/lowm/af.txt")).read().strip()
# what JR calls them besides their names (said on their own they're names too)
EXTRA = {0x0001: ["the Great One", "the Brahma Bull", "the People's Champion"], 0x0002: ["the Rattlesnake", "Steve Austin"],
         0x0003: ["the Phenom", "the Dead Man"], 0x000A: ["the Phenom", "the Dead Man"], 0x0004: ["the Big Red Monster"],
         0x0009: ["the Heartbreak Kid", "the Showstopper"], 0x0101: ["Hunter", "the Game", "the Cerebral Assassin"],
         0x0201: ["the World's Largest Athlete", "the Giant"], 0x0302: ["the Olympic hero"], 0x0604: ["the World's Strongest Man"],
         0x0606: ["the Eighth Wonder of the World"], 0x0507: ["Boss Man", "the Big Boss Man"], 0x0104: ["Road Dog", "the Road Dog"],
         0x0603: ["Davey Boy Smith", "Davey Boy"], 0x0103: ["Mr. Ass"], 0x0006: ["the Hardcore Legend"], 0x0503: ["D-Von"]}
def norm(s): return re.sub(r"[^a-z0-9 ]", "", s.lower().replace("-", " ").replace("'s", "s").replace("dogg", "dog"))
VARIANTS = []      # (tokens, id, display)
for i, vs in NAMES.items():
    for v in vs + EXTRA.get(i, []):
        VARIANTS.append((norm(v).split(), i, v))
VARIANTS.sort(key=lambda x: -len(x[0]))

def words(js):
    d = json.load(open(js))
    out = []
    for s in d["transcription"]:
        t = s["text"].strip()
        if not t: continue
        out.append((t, s["offsets"]["from"], s["offsets"]["to"]))
    return out

def load(src):
    """the clip as 20 kHz mono float (Legends': evened out as in the pack)"""
    af = [] if src.endswith(".wav") else ["-af", LOWM_AF]
    raw = subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-i", src] + af +
                         ["-ac", "1", "-ar", "20000", "-f", "s16le", "-"], capture_output=True).stdout
    return np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768

def quiet_point(x, a_ms, b_ms):
    """the quietest 10 ms in [a, b] (ms): where to cut"""
    a, b = max(0, int(a_ms * 20)), min(len(x), int(b_ms * 20))
    if b - a < 240: return (a + b) // 2
    e = [np.sum(x[k:k + 200] ** 2) for k in range(a, b - 200, 40)]
    return a + 40 * int(np.argmin(e)) + 100

def save(x, path):
    pcm = (np.clip(x, -1, 1) * 32767).astype(np.int16)
    w = wave.open(path, "wb"); w.setnchannels(1); w.setsampwidth(2); w.setframerate(20000); w.writeframes(pcm.tobytes()); w.close()

def main():
    os.makedirs(N + "cut", exist_ok=True)
    cuts = []
    for js in sorted(glob.glob(N + "ts/*.json")):
        base = os.path.basename(js)[:-5]               # 1_<wem>.wav or NNNN.wav
        key = (0, int(base[2:-4])) if base.startswith("1_") else (1, int(base[:-4]))
        src = nmp("/tmp/devbox-0/wwe13/wav/1_%d.wav") % key[1] if key[0] == 0 else nmp("/tmp/devbox-0/lowm/mp3/%04d.mp3") % key[1]
        W = words(js)
        toks = [norm(w[0]) for w in W]
        x = None
        for vt, wid, disp in VARIANTS:
            n = len(vt)
            for s in range(len(toks) - n + 1):
                if toks[s:s + n] != vt: continue
                # (not part of a longer name: "Stone Cold" in "Stone Cold Steve Austin")
                if any(toks[max(0, s - len(v2) + n):s + n] == v2[-(n + 0):] and len(v2) > n and toks[s:s + len(v2)] == v2
                       for v2, w2, _ in VARIANTS if w2 == wid): continue
                last = s + n == len(W)
                after_pause = not last and (W[s + n][1] - W[s + n - 1][2] >= 120 or re.search(r"[,.!?]$", W[s + n - 1][0]))
                if not (last or after_pause): continue
                if x is None: x = load(src)
                t0 = W[s][1]; t1 = W[s + n - 1][2]
                # (tight: whisper's word times run a little early/late; the quietest point near each edge, never
                # into the word before or after)
                prev_end = W[s - 1][2] if s > 0 else 0
                a = quiet_point(x, max(prev_end + 10, t0 - 90), t0 + 30)
                nxt = W[s + n][1] if not last else None
                b = len(x) if last else quiet_point(x, t1 - 10, min(nxt - 10, t1 + 200))
                if b - a < 2000: continue
                cuts.append(dict(kind="name", key=key, wid=wid, text=disp, a=a, b=b, src=key[0]))
                # the lead-in before it: "... by" / "... from" / "here comes" / "down goes"
                pre = [w[0] for w in W[:s]]
                if pre and last and s >= 2:
                    tail = norm(" ".join(pre[-2:]))
                    who = "*" if re.search(r"\b(by|from|of)$|here comes$", tail) else "v*" if re.search(r"down goes$|goes down$", tail) else None
                    if who:
                        la = quiet_point(x, 0, 60)
                        cuts.append(dict(kind="lead", key=key, who=who, text=" ".join(pre), a=0, b=a, src=key[0]))
                break
            else:
                continue
            break
    print(len(cuts), "cuts:", sum(c["kind"] == "name" for c in cuts), "names,", sum(c["kind"] == "lead" for c in cuts), "lead-ins")
    # write them, transcribe them again
    xs = {}
    for k, c in enumerate(cuts):
        src = nmp("/tmp/devbox-0/wwe13/wav/1_%d.wav") % c["key"][1] if c["key"][0] == 0 else nmp("/tmp/devbox-0/lowm/mp3/%04d.mp3") % c["key"][1]
        if src not in xs: xs = {src: load(src)}
        y = xs[src][c["a"]:c["b"]]
        fade = min(200, len(y) // 4)
        y = y.copy(); y[:fade] *= np.linspace(0, 1, fade); y[-fade:] *= np.linspace(1, 0, fade)
        c["file"] = N + "cut/%04d.wav" % k
        save(y, c["file"])
        c["ms"] = int(len(y) / 20)
        subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", c["file"], "-ar", "16000", N + "cut/%04d.16.wav" % k])
    files = [N + "cut/%04d.16.wav" % k for k in range(len(cuts))]
    for i in range(0, len(files), 60):
        cmd = [nmp("/tmp/devbox-0/wwe13/wcpp/build/bin/whisper-cli"), "-m", "/mnt/user-data/uploads/WWE 13/extracted/ggml-small.en-q5_1.bin",
               "-t", "2", "-nt", "-np", "-otxt", "-ac", "256", "-bs", "1", "-bo", "1"]
        for f in files[i:i + 60]: cmd += ["-f", f]
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ok = []
    for k, c in enumerate(cuts):
        f = N + "cut/%04d.16.wav.txt" % k
        heard = norm(open(f).read()) if os.path.exists(f) else ""
        want = norm(c["text"])
        if c["kind"] == "name":
            hw = heard.split(); ww = want.split()
            # (exactly the name: as many words, first and last the same -- "the Shawn Michaels" / "Michaels and" fail)
            good = len(hw) == len(ww) and hw[0][:3] == ww[0][:3] and hw[-1][:4] == ww[-1][:4]
        else:
            hw = heard.split(); ww = want.split()
            good = len(hw) > 0 and hw[-1][:3] == ww[-1][:3] and hw[0][:3] == ww[0][:3] and abs(len(hw) - len(ww)) <= 1
        c["heard"] = heard; c["ok"] = good
        if good: ok.append(c)
    print(len(ok), "pass the check:", sum(c["kind"] == "name" for c in ok), "names,", sum(c["kind"] == "lead" for c in ok), "lead-ins")
    for c in cuts:
        c["key"] = list(c["key"])
    json.dump(cuts, open(N + "splices.json", "w"), indent=0)

if __name__ == "__main__":
    main()
