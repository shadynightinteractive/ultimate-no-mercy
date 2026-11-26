#!/usr/bin/env python3
"""Real commentary cut short: the start of a line, up to (not including) what follows.
  leads : JR's lead-ins -- "What a move by", "Here comes", "Down goes" -- cut before the name that follows (any name,
          on the roster or not: only his words before it are kept); a name is joined on in the game
  elim  : a Rumble elimination's opening -- "He's gone!", "He's been eliminated!" -- cut before the name (Legends)
In: /tmp/devbox-0/leads/cands2.json, /tmp/devbox-0/elim/stubs.json; word times: whisper.cpp with DTW token times
(/tmp/devbox-0/leads/dtw/*.json). Out: <dir>/cut/*.wav + <dir>/out.json; each cut transcribed again and kept only if
it's those words, no more and no less."""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import json, os, re, sys, subprocess
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from mknames import load, quiet_point, save, norm
DTW = nmp("/tmp/devbox-0/leads/dtw/")
WHISPER = [nmp("/tmp/devbox-0/wwe13/wcpp/build/bin/whisper-cli"), "-m", "/mnt/user-data/uploads/WWE 13/extracted/ggml-small.en-q5_1.bin"]

def dtw_words(js):
    """[(word, first token's time ms, last token's time ms, segment from, segment to)]"""
    out = []
    for s in json.load(open(js))["transcription"]:
        t = s["text"].strip()
        tk = [x for x in s.get("tokens", []) if x.get("t_dtw", -1) >= 0 and not x["text"].startswith("[")]
        if not t or not tk: continue
        out.append((t, tk[0]["t_dtw"] * 10, tk[-1]["t_dtw"] * 10, s["offsets"]["from"], s["offsets"]["to"]))
    return out

def cut(items, d):
    os.makedirs(d + "cut", exist_ok=True)
    out = []
    for x in items:
        js = DTW + os.path.basename(x["w16"]) + ".json"
        if not os.path.exists(js): continue
        W = dtw_words(js); toks = [norm(w[0]) for w in W]
        want = norm(x["lead"]).split()
        # the lead's words in order from the start (whisper may split or join one or two)
        k = 0; j = 0
        while k < len(toks) and j < len(want):
            if toks[k][:3] == want[j][:3] or want[j].startswith(toks[k][:3]) or toks[k].startswith(want[j][:3]): j += 1
            elif j == 0 and k > 2: break
            k += 1
        if j < len(want) or k >= len(toks): continue
        y = load(x["src"])
        last_t = W[k - 1][2]; next_t = W[k][1]
        b = quiet_point(y, last_t + 60, max(last_t + 120, next_t - 15))
        a = quiet_point(y, 0, min(W[0][1], 120))
        z = y[a:b].copy()
        if len(z) < 4000: continue
        f = min(160, len(z) // 4); z[:f] *= np.linspace(0, 1, f); z[-f:] *= np.linspace(1, 0, f)
        n = len(out)
        x["file"] = d + "cut/%04d.wav" % n; x["ms"] = int(len(z) / 20)
        save(z, x["file"])
        subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", x["file"], "-ar", "16000", d + "cut/%04d.16.wav" % n])
        out.append(x)
    files = [d + "cut/%04d.16.wav" % n for n in range(len(out))]
    for i in range(0, len(files), 60):
        cmd = WHISPER + ["-t", "2", "-nt", "-np", "-otxt", "-ac", "256", "-bs", "1", "-bo", "1"]
        for f in files[i:i + 60]: cmd += ["-f", f]
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for n, x in enumerate(out):
        f = d + "cut/%04d.16.wav.txt" % n
        heard = norm(open(f).read()).split() if os.path.exists(f) else []
        want = norm(x["lead"]).split()
        x["heard"] = " ".join(heard)
        x["ok"] = bool(heard) and heard[-1][:3] == want[-1][:3] and heard[0][:2] == want[0][:2] and abs(len(heard) - len(want)) <= 1
    json.dump(out, open(d + "out.json", "w"), indent=0)
    print(d, len(items), "lines,", len(out), "cut,", sum(x["ok"] for x in out), "pass")
    for x in out:
        if not x["ok"]: print("  x", x["lead"], "| heard:", x["heard"])
    return out

def main():
    leads = json.load(open(nmp("/tmp/devbox-0/leads/cands2.json")))
    cut(leads, nmp("/tmp/devbox-0/leads/"))
    stubs = json.load(open(nmp("/tmp/devbox-0/elim/stubs.json")))
    for x in stubs:
        x["lead"] = x["stub"]; x["src"] = nmp("/tmp/devbox-0/lowm/mp3/%04d.mp3") % x["key"][1]
        x["w16"] = nmp("/tmp/devbox-0/lowm/w16/%04d.wav") % x["key"][1]
    cut(stubs, nmp("/tmp/devbox-0/elim/"))
if __name__ == "__main__":
    main()
