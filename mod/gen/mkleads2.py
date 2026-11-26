#!/usr/bin/env python3
"""mkleads, with the cut found by listening: JR runs "by" straight into the name, so for each line a handful of cut
points around the end of his last word (DTW time -60..+60 ms) are each transcribed (padded with silence); the cut is
the middle of those heard as exactly the lead-in's words. Out: <dir>/cut2/*.wav, <dir>/out2.json"""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import json, os, sys, subprocess
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from mknames import load, save, norm
from mkleads import dtw_words, DTW, WHISPER

def heard_ok(heard, want):
    h = norm(heard).split(); w = norm(want).split()
    if not h or len(h) != len(w): return False
    return all(a[:3] == b[:3] or a.startswith(b[:2]) and len(b) <= 2 for a, b in zip(h, w))

def find(items, d, offs=range(-40, 21, 20)):
    os.makedirs(d + "try", exist_ok=True); os.makedirs(d + "cut2", exist_ok=True)
    tries = []
    for n, x in enumerate(items):
        js = DTW + os.path.basename(x["w16"]) + ".json"
        if not os.path.exists(js): continue
        W = dtw_words(js); toks = [norm(w[0]) for w in W]
        want = norm(x["lead"]).split()
        k = 0; j = 0
        while k < len(toks) and j < len(want):
            if toks[k][:3] == want[j][:3] or want[j].startswith(toks[k][:3]) or toks[k].startswith(want[j][:3]): j += 1
            elif j == 0 and k > 2: break
            k += 1
        if j < len(want) or k >= len(toks): continue
        y = load(x["src"]); lt = W[k - 1][2]
        x["_y"] = y; x["_lt"] = lt; x["_n"] = n
        for o in offs:
            b = (lt + o) * 20
            if b <= 4000 or b > len(y): continue
            z = np.concatenate([y[:b], np.zeros(6000, dtype=np.float32)])
            f = d + "try/%04d_%+04d.16.wav" % (n, o)
            pcm = (np.clip(z[::5] if False else z, -1, 1) * 32767).astype(np.int16)
            save(z, f[:-7] + ".wav")
            subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", f[:-7] + ".wav", "-ar", "16000", f])
            tries.append((x, o, f))
    todo = [f for _, _, f in tries if not os.path.exists(f + ".txt")]
    for i in range(0, len(todo), 80):
        # (a short window: these are a few seconds; the full 30 s one is 4x slower)
        cmd = WHISPER + ["-t", "2", "-nt", "-np", "-otxt", "-bs", "1", "-bo", "1", "-ac", "448"]
        for f in todo[i:i + 80]: cmd += ["-f", f]
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    good = {}
    for x, o, f in tries:
        t = open(f + ".txt").read().strip() if os.path.exists(f + ".txt") else ""
        x.setdefault("_heard", {})[o] = t
        if heard_ok(t, x["lead"]): good.setdefault(id(x), []).append(o)
    out = []
    for x in items:
        if "_y" not in x: continue
        g = sorted(good.get(id(x), []))
        x["heard"] = x["_heard"]
        if not g: x["ok"] = False; out.append(x); continue
        o = g[len(g) // 2]
        y = x["_y"]; b = (x["_lt"] + o) * 20
        z = y[:b].copy()
        # (the start: from the quiet before his first word)
        s = 0
        while s < len(z) - 400 and np.max(np.abs(z[s:s + 400])) < 0.01: s += 200
        z = z[max(0, s - 400):]
        f = min(160, len(z) // 4); z[:f] *= np.linspace(0, 1, f); z[-f:] *= np.linspace(1, 0, f)
        x["file"] = d + "cut2/%04d.wav" % len(out); x["ms"] = int(len(z) / 20); x["ok"] = True; x["off"] = o
        save(z, x["file"]); out.append(x)
    for x in out:
        for k in ("_y", "_lt", "_n", "_heard"): x.pop(k, None)
    json.dump(out, open(d + "out2.json", "w"), indent=0)
    print(d, len(items), "lines,", sum(x["ok"] for x in out), "cut clean")
    for x in out:
        if not x["ok"]: print("  x", x["lead"], "|", x["heard"])
    return out

if __name__ == "__main__":
    leads = json.load(open(nmp("/tmp/devbox-0/leads/cands2.json")))
    find(leads, nmp("/tmp/devbox-0/leads/"))
    stubs = json.load(open(nmp("/tmp/devbox-0/elim/stubs.json")))
    for x in stubs:
        x["lead"] = x["stub"]; x["src"] = nmp("/tmp/devbox-0/lowm/mp3/%04d.mp3") % x["key"][1]
        x["w16"] = nmp("/tmp/devbox-0/lowm/w16/%04d.wav") % x["key"][1]
    find(stubs, nmp("/tmp/devbox-0/elim/"), range(0, 161, 40))
