#!/usr/bin/env python3
"""Names cut by listening (as mkleads2): JR's own (WWE '13 + Legends, /tmp/devbox-0/names2/cands.json) and the AI
voice's (the 2-hour model's "Here comes <name>!", /tmp/devbox-0/ainames). Word times: whisper.cpp DTW.
Start: cut points before the name's first sound (its DTW time -160..+20 ms) are each transcribed; then, the start
chosen, its end the same way (line-final names: the line's end). The cut is the middle of those heard as exactly the
name. Out: <dir>/cut/*.wav, <dir>/out.json"""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import json, os, sys, subprocess, re, glob
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from mknames import load, save, norm
from mkleads import dtw_words, WHISPER

def same(heard, pats):
    return any(same1(heard, p) for p in pats)
def same1(heard, want):
    h = norm(heard).split(); w = norm(want).split()
    return bool(h) and len(h) == len(w) and all(a[:4] == b[:4] or a[:3] == b[:3] and len(b) <= 4 for a, b in zip(h, w))

def transcribe(files):
    todo = [f for f in files if not os.path.exists(f + ".txt")]
    for i in range(0, len(todo), 80):
        cmd = WHISPER + ["-t", "2", "-nt", "-np", "-otxt", "-bs", "1", "-bo", "1", "-ac", "448"]
        for f in todo[i:i + 80]: cmd += ["-f", f]
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return [open(f + ".txt").read().strip() if os.path.exists(f + ".txt") else "" for f in files]

def clip16(z, f):
    save(np.concatenate([np.zeros(4000, dtype=np.float32), z, np.zeros(6000, dtype=np.float32)]), f[:-7] + ".wav")
    subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", f[:-7] + ".wav", "-ar", "16000", f])

def locate(W, pats):
    """(first word, past-last word) of the name in the words, or None"""
    toks = [norm(w[0]) for w in W]
    for p in pats:
        pw = norm(p).split(); n = len(pw)
        for s in range(len(toks) - n + 1):
            if all(toks[s + i][:3] == pw[i][:3] for i in range(n)): return s, s + n
    # (whisper may join or split a name: by letters)
    joined = "".join(toks)
    return None

def quick(items, d):
    """one cut per line at the quietest points around the name; those heard as exactly the name are done"""
    from mknames import quiet_point
    os.makedirs(d + "try", exist_ok=True)
    tries = []
    for n, x in enumerate(items):
        if not os.path.exists(x["dtw"]): continue
        W = dtw_words(x["dtw"]); loc = locate(W, x["pats"])
        if not loc: continue
        s, e = loc; y = load(x["src"])
        prev = W[s - 1][2] if s > 0 else 0
        a = quiet_point(y, max(0, prev + 30) if s > 0 else 0, max(prev + 60, W[s][1] - 30)) if s > 0 else 0
        b = len(y) if e >= len(W) else quiet_point(y, W[e - 1][2] + 10, max(W[e - 1][2] + 50, W[e][1] - 20))
        if b - a < 2000: continue
        f = d + "try/%04d_q.16.wav" % n; clip16(y[a:b], f); tries.append((x, a, b, f, y, e >= len(W)))
    done, rest = [], []
    for (x, a, b, f, y, final), t in zip(tries, transcribe([f for *_, f, _, _ in tries])):
        if same(t, x["pats"]):
            z = y[a:b].copy()
            if final:
                e = len(z)
                while e > 2000 and np.max(np.abs(z[e - 200:e])) < 0.008: e -= 100
                z = z[:min(len(z), e + 600)]
            g = min(160, len(z) // 4); z[:g] *= np.linspace(0, 1, g); z[-g:] *= np.linspace(1, 0, g)
            x["_z"] = z; x["heard"] = t; done.append(x)
    ids = {id(x) for x in done}
    rest = [x for x in items if id(x) not in ids]
    print(d, "quick:", len(done), "clean of", len(tries))
    return done, rest

def run(items, d):
    os.makedirs(d + "try", exist_ok=True); os.makedirs(d + "cut", exist_ok=True)
    live = []
    for n, x in enumerate(items):
        if not os.path.exists(x["dtw"]): continue
        W = dtw_words(x["dtw"])
        loc = locate(W, x["pats"])
        if not loc: continue
        s, e = loc
        x["_y"] = load(x["src"]); x["_t0"] = W[s][1]; x["_t1"] = W[e - 1][2]
        x["_final"] = e >= len(W); x["_n"] = n
        x["_prev"] = W[s - 1][2] if s > 0 else 0
        live.append(x)
    # the start
    tries = []
    for x in live:
        y = x["_y"]; end = len(y) if x["_final"] else min(len(y), (x["_t1"] + 60) * 20)
        for o in range(-150, 1, 50):
            a = (x["_t0"] + o) * 20
            if a < x["_prev"] * 20 - 400 or a < 0 or end - a < 2000: continue
            f = d + "try/%04d_s%+04d.16.wav" % (x["_n"], o); clip16(y[a:end], f); tries.append((x, o, f))
    for (x, o, f), t in zip(tries, transcribe([f for _, _, f in tries])):
        x.setdefault("_hs", {})[o] = t
        if same(t, x["pats"]): x.setdefault("_gs", []).append(o)
    # the end (names mid-line)
    tries = []
    for x in live:
        if not x.get("_gs"): continue
        g = sorted(x["_gs"]); x["_a"] = (x["_t0"] + g[len(g) // 2]) * 20
        if x["_final"]: x["_b"] = len(x["_y"]); continue
        for o in range(-20, 61, 40):
            b = (x["_t1"] + o) * 20
            if b - x["_a"] < 2000 or b > len(x["_y"]): continue
            f = d + "try/%04d_e%+04d.16.wav" % (x["_n"], o); clip16(x["_y"][x["_a"]:b], f); tries.append((x, o, f))
    for (x, o, f), t in zip(tries, transcribe([f for _, _, f in tries])):
        x.setdefault("_he", {})[o] = t
        if same(t, x["pats"]): x.setdefault("_ge", []).append(o)
    out = []
    for x in live:
        ok = bool(x.get("_gs")) and (x["_final"] or bool(x.get("_ge")))
        if ok:
            if not x["_final"]:
                g = sorted(x["_ge"]); x["_b"] = (x["_t1"] + g[len(g) // 2]) * 20
            z = x["_y"][x["_a"]:x["_b"]].copy()
            # (a line-final name: its silence after trimmed)
            if x["_final"]:
                e = len(z)
                while e > 2000 and np.max(np.abs(z[e - 200:e])) < 0.008: e -= 100
                z = z[:min(len(z), e + 600)]
            f = min(160, len(z) // 4); z[:f] *= np.linspace(0, 1, f); z[-f:] *= np.linspace(1, 0, f)
            x["file"] = d + "cut/%04d.wav" % len(out); x["ms"] = int(len(z) / 20)
            save(z, x["file"])
        x["ok"] = ok; x["heard"] = {"start": x.get("_hs"), "end": x.get("_he")}
        out.append(x)
    for x in out:
        for k in list(x):
            if k.startswith("_"): x.pop(k)
    json.dump(out, open(d + "out.json", "w"), indent=0)
    print(d, len(items), "lines,", len(live), "found the name,", sum(x["ok"] for x in out), "cut clean")
    return out

def both(items, d):
    done, rest = quick(items, d)
    out = run(rest, d)
    os.makedirs(d + "cut", exist_ok=True)
    k = len(out)
    for x in done:
        x["file"] = d + "cut/q%04d.wav" % k; k += 1; x["ms"] = int(len(x["_z"]) / 20); x["ok"] = True
        save(x.pop("_z"), x["file"])
        for kk in list(x):
            if kk.startswith("_"): x.pop(kk)
    allx = out + done
    json.dump(allx, open(d + "out.json", "w"), indent=0)
    print(d, "total clean:", sum(x["ok"] for x in allx))
    return allx

def real():
    c = json.load(open(nmp("/tmp/devbox-0/names2/cands.json")))
    ALT = {"Val Venis": ["val venis", "val venus"], "Venis": ["venis", "venus"], "Tazz": ["taz", "tazz"], "Faarooq": ["farooq", "faruk"],
           "X-Pac": ["x pac", "xpac"], "Grand Master Sexay": ["grandmaster sexay", "grand master sexay", "grandmaster sexy"],
           "Scotty 2 Hotty": ["scotty 2 hotty", "scotty two hotty", "scotty too hotty"]}
    for x in c:
        x["dtw"] = nmp("/tmp/devbox-0/names2/dtw/") + os.path.basename(x["w16"]) + ".json"
        x["pats"] = ALT.get(x["variant"], [x["variant"]]); x["want"] = x["pats"][0]
    return both(c, nmp("/tmp/devbox-0/names2/"))

def ai():
    # (2026-11-14: the AI voice is scrapped -- the user; its model and lines_names.json are deleted. What follows is
    #  only the record of how the 13 AI names, since taken out of the pack, were cut.)
    raise SystemExit("mknames2 ai: the AI voice is scrapped (2026-11-14); nothing to cut")
    job = json.load(open(nmp("/mnt/user-data/outputs/voicelab5/lines_names.json")))
    items = []
    for ln in job["lines"]:
        for t in range(len(job["takes"])):
            b = "%s_%d" % (ln["id"], t)
            items.append(dict(id=b, wid=int(ln["id"][1:4], 16), variant=ln["name"], src=nmp("/tmp/devbox-0/ainames/src/%s.wav") % b,
                              dtw=nmp("/tmp/devbox-0/ainames/%s.16.wav.json") % b, pats=[ln["name"], ln["say"]], want=ln["name"]))
    # (how whisper writes them back: the spelling it heard is fine when it's the sound asked for)
    for x in items: x["pats"] = list(dict.fromkeys(x["pats"]))
    return both(items, nmp("/tmp/devbox-0/ainames/"))

if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "real"
    real() if which == "real" else ai()
