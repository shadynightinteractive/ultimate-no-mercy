"""Native encode (comm_encode_test.exe) vs FFmpeg + oggpk.replace_comment (rebuild.py's own chain), byte for byte.
Inputs: pack clips decoded to s16 (any real speech will do), with silence / quiet noise put around them so the trim
has something to cut. usage: python -I test_encode.py <clips folder> <count> <exe> [ffmpeg]"""
import os, sys, subprocess, tempfile, random, struct, wave, hashlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oggpk

TRIM = ("silenceremove=start_periods=1:start_threshold=-50dB,areverse,"
        "silenceremove=start_periods=1:start_threshold=-50dB,areverse")
COMMENT = bytes.fromhex("03766f726269730d0000004c61766636302e31362e313030010000001f000000656e636f6465723d4c61766336302e33312e313032206c6962766f7262697301")

def main():
    d, count, exe = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    ff = sys.argv[4] if len(sys.argv) > 4 else "ffmpeg"
    rnd = random.Random(1)
    fs = sorted(f for f in os.listdir(d) if f.endswith(".ogg"))
    rnd.shuffle(fs)
    tmp = tempfile.mkdtemp(prefix="c2enc_")
    same = diff = 0; bad = []
    for k, f in enumerate(fs[:count]):
        raw = subprocess.run([ff, "-nostdin", "-loglevel", "error", "-i", os.path.join(d, f), "-ac", "1", "-f", "s16le", "-"],
                             capture_output=True, check=True).stdout
        x = list(struct.unpack("<%dh" % (len(raw) // 2), raw))
        # quiet edges: zeros, then low noise below / around the -50 dB threshold (~104 / 32768)
        mode = k % 4
        pre = [0] * rnd.randint(0, 3000) + [rnd.randint(-60, 60) for _ in range(rnd.randint(0, 2000))]
        post = [rnd.randint(-150, 150) for _ in range(rnd.randint(0, 2000))] + [0] * rnd.randint(0, 3000)
        if mode == 3: pre, post = [], []
        y = pre + x + post
        rate = 20000 if mode != 2 else 22050
        rp = os.path.join(tmp, "in.raw"); wp = os.path.join(tmp, "in.wav")
        open(rp, "wb").write(struct.pack("<%dh" % len(y), *y))
        with wave.open(wp, "wb") as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate); w.writeframes(open(rp, "rb").read())
        serial = rnd.randint(0, 2**32 - 1)
        notrim = (k % 5 == 4)
        ref = os.path.join(tmp, "ref.ogg"); nat = os.path.join(tmp, "nat.ogg")
        af = "anull" if notrim else TRIM
        subprocess.run([ff, "-nostdin", "-loglevel", "error", "-y", "-i", wp, "-ac", "1", "-af", af,
                        "-c:a", "libvorbis", "-q:a", "0", ref], check=True)
        rb = oggpk.replace_comment(open(ref, "rb").read(), COMMENT, serial)
        args = [exe, rp, str(rate), str(serial), COMMENT.hex(), nat] + (["notrim"] if notrim else [])
        subprocess.run(args, check=True, capture_output=True)
        nb = open(nat, "rb").read()
        if nb == rb: same += 1
        else:
            diff += 1
            # where: audio packets equal? granules?
            pr, pn = oggpk.packets(rb), oggpk.packets(nb)
            pk_same = [a for a, _ in pr] == [a for a, _ in pn]
            bad.append((f, mode, notrim, len(rb), len(nb), len(pr), len(pn), pk_same, pr[-1][1], pn[-1][1]))
    print("same %d, different %d" % (same, diff))
    for b in bad[:20]: print("  ", b)

if __name__ == "__main__":
    main()
