"""Made-up ENCRYPTED Wii disc images (plain .iso and .wbfs), encrypted with a made-up test common key -- never a real
key -- to test the game's reader of plain Wii images with the player's key file (packbuild/wii_disc.cpp
EncryptedWiiDisc, aes128.cpp, find_wii_keys). The AES here is Python's 'cryptography' package, independent of ours.

  python -I test_wii_crypt.py <work dir> <wii_disc_test.exe> [<comm_build_test.exe> <recipe dir> <made-up disc folder>]

Checks: every file of the made-up file system read back byte for byte from .iso and .wbfs, with the key given as
common-key.bin (16 bytes), common_key.txt (hex) and BootMii's keys.bin (key at 0x114); refused with the right reason
with no key file and with a wrong key; with the builder's arguments, the whole commentary builder on the .iso.
"""
import hashlib, os, random, shutil, struct, subprocess, sys
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

SECTOR, DATA = 0x8000, 0x7C00

def cbc_enc(key, iv, b):
    e = Cipher(algorithms.AES(key), modes.CBC(iv)).encryptor()
    return e.update(b) + e.finalize()

def fst_and_boot(files, game_id, shift=2):
    """the partition's plaintext data: boot header, FST (Wii: offsets >> 2), the files; -> bytes, {path: (off, data)}"""
    entries, names = [], bytearray()
    def name_off(n):
        o = len(names); names.extend(n.encode() + b"\0"); return o
    tree = {}
    for p, _ in files:
        node = tree
        for d in p.split("/")[:-1]: node = node.setdefault(d + "/", {})
        node[p.split("/")[-1]] = p
    placed = {}
    entries.append([1, 0, 0, 0])
    def walk(node, parent):
        for k in sorted(node):
            if k.endswith("/"):
                i = len(entries); entries.append([1, name_off(k[:-1]), parent, 0]); walk(node[k], i); entries[i][3] = len(entries)
            else:
                placed[node[k]] = len(entries); entries.append([0, name_off(k), 0, 0])
    walk(tree, 0); entries[0][3] = len(entries)
    fst_off = 0x2440
    fst_size = (len(entries) * 12 + len(names) + 3) & ~3
    pos = (fst_off + fst_size + 0x8000) & ~0x7FFF
    spots = {}
    for p, b in files:
        spots[p] = pos; pos = (pos + len(b) + 0x1F) & ~0x1F
    space = bytearray(pos + 0x10000)
    for p, b in files:
        space[spots[p]:spots[p] + len(b)] = b
        e = entries[placed[p]]; e[2], e[3] = spots[p] >> shift, len(b)
    fst = b"".join(struct.pack(">III", (t << 24) | n, a, c) for t, n, a, c in entries) + names
    space[fst_off:fst_off + len(fst)] = fst
    boot = bytearray(0x440); boot[0:6] = game_id; boot[0x18:0x1C] = struct.pack(">I", 0x5D1C9EA3)
    boot[0x20:0x30] = b"Made-up Wii disc"
    boot[0x424:0x42C] = struct.pack(">II", fst_off >> shift, fst_size >> shift)
    space[0:0x440] = boot
    return bytes(space), {p: (spots[p], b) for p, b in files}

def make_iso(files, common_key, rng, game_id=b"ZTSTW2"):
    plain, spots = fst_and_boot(files, game_id)
    nsec = (len(plain) + DATA - 1) // DATA
    plain = plain.ljust(nsec * DATA, b"\0")
    title_key = rng.randbytes(16); title_id = rng.randbytes(8)
    part_off, data_rel = 0x50000, 0x20000
    iso = bytearray(part_off + data_rel + nsec * SECTOR)
    iso[0:6] = game_id; iso[0x18:0x1C] = struct.pack(">I", 0x5D1C9EA3); iso[0x20:0x30] = b"Made-up Wii disc"
    iso[0x40000:0x40020] = struct.pack(">8I", 1, 0x40020 >> 2, 0, 0, 0, 0, 0, 0)
    iso[0x40020:0x40028] = struct.pack(">II", part_off >> 2, 0)
    tk = bytearray(0x2C0)
    tk[0x1BF:0x1CF] = cbc_enc(common_key, title_id + bytes(8), title_key)
    tk[0x1DC:0x1E4] = title_id
    tk[0x2B8:0x2C0] = struct.pack(">II", data_rel >> 2, (nsec * SECTOR) >> 2)
    iso[part_off:part_off + 0x2C0] = tk
    for s in range(nsec):
        h = cbc_enc(title_key, bytes(16), rng.randbytes(0x400))         # (a made-up hash block, encrypted)
        d = cbc_enc(title_key, h[0x3D0:0x3E0], plain[s * DATA:(s + 1) * DATA])
        at = part_off + data_rel + s * SECTOR
        iso[at:at + SECTOR] = h + d
    return bytes(iso), spots

def make_wbfs(iso, shift=21):
    """a one-disc .wbfs: header, disc header copy + block table at the second 512-byte sector, blocks stored from 1 on"""
    bs = 1 << shift
    blocks = (143432 * 2 * SECTOR) >> shift
    out = bytearray(bs)                                                 # block 0: the header and tables
    wlba = [0] * blocks
    nxt = 1
    for i in range((len(iso) + bs - 1) // bs):
        b = iso[i * bs:(i + 1) * bs].ljust(bs, b"\0")
        if not any(b): continue                                         # (an empty block isn't stored)
        wlba[i] = nxt; nxt += 1; out += b
    out[0:12] = b"WBFS" + struct.pack(">I", len(out) // 512) + bytes([9, shift, 0, 0])
    out[12] = 1                                                         # disc table: slot 0 used
    out[512:512 + 0x100] = iso[:0x100]
    out[512 + 0x100:512 + 0x100 + 2 * blocks] = struct.pack(">%dH" % blocks, *wlba)
    return bytes(out)

def run(args):
    r = subprocess.run(args, capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr

def main():
    work, disc_exe = sys.argv[1], sys.argv[2]
    rng = random.Random(1118)
    shutil.rmtree(work, ignore_errors=True); os.makedirs(work)
    key = rng.randbytes(16)                                             # a made-up common key, for this test only
    files = [("opening.bnr", rng.randbytes(0x1960)), ("Audio/English(US)/tiny.txt", b"hello"),
             ("Data/odd size.bin", rng.randbytes(77777)), ("Data/zeros.bin", bytes(100000)),
             ("Audio/English(US)/speech.bin", rng.randbytes(300003))]
    builder = len(sys.argv) > 5
    if builder:
        pck = os.path.join(sys.argv[5], "files", "Audio", "English(US)", "Speech.pck")
        files.append(("Audio/English(US)/Speech.pck", open(pck, "rb").read()))
    iso, spots = make_iso(files, key, rng)
    ok = bad = 0
    def check(name, cond, detail=""):
        nonlocal ok, bad
        if cond: ok += 1
        else: bad += 1; print("FAIL", name, detail[-300:])
    for form in ("iso", "wbfs"):
        d = os.path.join(work, form); os.makedirs(d)
        img = os.path.join(d, "test." + form)
        open(img, "wb").write(iso if form == "iso" else make_wbfs(iso))
        # no key file: refused, saying to add the key file or convert
        rc, out = run([disc_exe, img, "list"])
        check(form + " no key", rc != 0 and "key file" in out and "RVZ" in out, out)
        # a wrong key: refused
        open(os.path.join(d, "common-key.bin"), "wb").write(rng.randbytes(16))
        rc, out = run([disc_exe, img, "list"])
        check(form + " wrong key", rc != 0 and "does not open this disc" in out, out)
        os.remove(os.path.join(d, "common-key.bin"))
        for kind in ("common-key.bin", "common_key.txt", "keys.bin"):
            kp = os.path.join(d, kind)
            if kind.endswith(".txt"): open(kp, "w").write("0x" + key.hex().upper() + "\n")
            elif kind == "keys.bin": b = bytearray(rng.randbytes(0x400)); b[0x114:0x124] = key; open(kp, "wb").write(b)
            else: open(kp, "wb").write(key)
            rc, out = run([disc_exe, img, "list"])
            check(form + " " + kind + " list", rc == 0 and all(p in out for p in spots), out)
            for p, (_, b) in spots.items():
                o = os.path.join(d, "x.bin")
                rc, out = run([disc_exe, img, "extract", p, o])
                check(form + " " + kind + " " + p, rc == 0 and open(o, "rb").read() == b, out)
            os.remove(kp)
    print("encrypted images: %d checks passed, %d failed" % (ok, bad))
    if builder:
        bexe, recipe = sys.argv[3], sys.argv[4]
        d = os.path.join(work, "iso"); open(os.path.join(d, "common-key.bin"), "wb").write(key)
        rc, out = run([bexe, recipe, os.path.join(d, "test.iso"), os.path.join(work, "from_iso.pak"), "4"])
        print("\n".join(l for l in out.splitlines() if not l.startswith("  ")))
        os.remove(os.path.join(d, "common-key.bin"))
    sys.exit(1 if bad else 0)

if __name__ == "__main__":
    main()
