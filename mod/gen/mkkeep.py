#!/usr/bin/env python3
"""The splice bank, from what was kept in QA (REVIEW\\Keep on the PC -> /tmp/devbox-0/keep/Keep):
  Keep/*.wav            JR's names: "<wrestler> - <name> (n).wav"
  Keep/Sentences/*.wav  his lead-ins: "<words> ... (n).wav" -- (NNNN): names/splices.json's cut; (n): leads/out2.json's
  Keep/Rumble/*.wav     a Royal Rumble's eliminations: "(whole line) ..." (ELIM_LINES) and openings cut from Legends'
Each: its pitch (median F0, Hz) and loudness (dB) at the join -- a lead-in's last 400 ms, a name's first 400 ms -- so
the director can join a lead-in with the take of the name that sounds most like it. Out: /tmp/devbox-0/keep/keep.json"""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import glob, json, os, re, sys, subprocess, collections
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from names import NAMES
K = nmp("/tmp/devbox-0/keep/Keep/")
def rd(f):
    raw = subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-i", f, "-ac", "1", "-ar", "20000", "-f", "s16le", "-"], capture_output=True).stdout
    return np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768
def f0_db(x):
    """median pitch of the voiced 40 ms frames (autocorrelation, 70-400 Hz) and their loudness"""
    fr, f0s, dbs = 800, [], []
    for i in range(0, len(x) - fr, 200):
        w = x[i:i + fr] * np.hanning(fr)
        e = np.sqrt(np.mean(w ** 2))
        if e < 0.01: continue
        ac = np.correlate(w, w, "full")[fr - 1:]
        lo, hi = 20000 // 400, 20000 // 70
        k = lo + int(np.argmax(ac[lo:hi]))
        if ac[k] > 0.35 * ac[0]: f0s.append(20000 / k); dbs.append(20 * np.log10(e))
    return (float(np.median(f0s)) if f0s else 0.0, float(np.median(dbs)) if dbs else -40.0)
def safe(s): return re.sub(r'[<>:"/\\|?*]', "", s).strip(" .")
def main():
    out = []
    disp = {}
    for i, vs in NAMES.items():
        disp.setdefault(vs[0], i)          # (the first: 0x0003 The Undertaker; 0x000A gets the same names in the pack)
    for f in sorted(glob.glob(K + "*.wav")):
        m = re.match(r"(.+?) - (.+) \(\d+\)\.wav$", os.path.basename(f))
        w = disp[m.group(1)]
        x = rd(f); f0, db = f0_db(x[:8000])
        out.append(dict(kind="name", wid=w, text=m.group(2), file=f, f0=f0, db=db, ms=int(len(x) / 20)))
    # where each kept lead-in came from
    old = json.load(open(nmp("/tmp/devbox-0/names/splices.json")))
    new = [x for x in json.load(open(nmp("/tmp/devbox-0/leads/out2.json"))) if x["ok"]]
    have = {tuple(x["key"]) for x in old if x["kind"] == "lead"}
    def key_of(x): return (0, int(x["id"][4:])) if x["id"].startswith("w13_") else (1, int(x["id"][4:]))
    numbered = {}; seen = collections.Counter()
    for x in sorted(new, key=lambda x: x["lead"].lower()):
        if key_of(x) in have: continue
        t = safe(x["lead"])[:80]; seen[t] += 1; numbered["%s ... (%d).wav" % (t, seen[t])] = (key_of(x), x["lead"])
    for f in sorted(glob.glob(K + "Sentences/*.wav")):
        b = os.path.basename(f)
        m = re.match(r"(.+) \.\.\. \((\d{4})\)\.wav$", b)
        if m: key, text = tuple(old[int(m.group(2))]["key"]), old[int(m.group(2))]["text"]
        else: key, text = numbered[b]
        x = rd(f); f0, db = f0_db(x[-8000:])
        out.append(dict(kind="lead", key=list(key), text=text, file=f, f0=f0, db=db, ms=int(len(x) / 20)))
    for f in sorted(glob.glob(K + "Rumble/*.wav")):
        b = os.path.basename(f)
        spk = "J" if " JR - " in b or b.startswith("JR - ") else "K" if "King - " in b else "-"
        t = re.sub(r"^\(whole line\) ", "", b[:-4]); t = re.sub(r"^(JR|King) - ", "", t); t = re.sub(r" \(\d+\)$", "", t)
        x = rd(f)
        out.append(dict(kind="elim", text=t, spk=spk, file=f, whole=b.startswith("(whole line)"), ms=int(len(x) / 20)))
    json.dump(out, open(nmp("/tmp/devbox-0/keep/keep.json"), "w"), indent=0)
    c = collections.Counter(x["kind"] for x in out); print(dict(c))
    print("wrestlers named:", len({x["wid"] for x in out if x["kind"] == "name"}))
if __name__ == "__main__":
    main()
