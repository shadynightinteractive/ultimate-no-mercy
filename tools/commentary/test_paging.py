"""Model of ffmpeg's Ogg muxer + libvorbis wrapper paging (oggenc.c, libvorbisenc.c, audio_frame_queue.c,
vorbis_parser.c), checked against the real pack clips: re-page each clip's packets with the model and compare bytes.
usage: python test_paging.py <folder of .ogg clips> [limit]"""
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oggpk

def rescale(a, sr):            # av_rescale_q(a, 1/sr, 1/1000000): round to nearest, halves away from zero
    num = a * 1000000
    if num >= 0: return (num + sr // 2) // sr
    return -((-num + sr // 2) // sr)

def audio_pages(packets, bs, total, sr, initial_padding):
    """packets: audio packets (bytes); bs: (short, long); total: the stream's samples (last granule)"""
    prev = bs[0]          # (vorbis parser: previous_blocksize = blocksize[mode 0]) ...
    prev = bs[0]          # ... then the priming packet (a short block) set it again: short
    remaining = total + initial_padding
    pts0 = -initial_padding
    out = []              # (flags, granule, lacing, body)
    page = None; start = pts0; done = 0
    for p in packets:
        cur = bs[(p[0] >> 1) & 1]
        dur = (prev + cur) >> 2; prev = cur
        rem = min(dur, remaining); remaining -= rem
        pts = pts0 + done; done += rem
        gran = pts + rem
        if page is None: page = [0, -1, [], b""]
        if page[2] and 65025 - len(page[3]) < len(p):
            out.append(page); start = page[1]; page = [0, -1, [], b""]
        total_seg = len(p) // 255 + 1; i = 0; q = p
        while i < total_seg:
            segs = min(total_seg - i, 255 - len(page[2]))
            if i and not page[2]: page[0] |= 1
            page[2] += [255] * (segs - 1)
            ln = min(len(q), segs * 255)
            page[2].append(ln - (segs - 1) * 255)
            page[3] += q[:ln]; q = q[ln:]; i += segs
            if i == total_seg: page[1] = gran
            if len(page[2]) == 255 or rescale(page[1], sr) - rescale(start, sr) >= 1000000:
                out.append(page); start = page[1]; page = [0, -1, [], b""]
    if page is not None and page[2]: out.append(page)
    out[-1][0] |= 4
    return out

def rebuild(b, initial_padding):
    pk = oggpk.packets(b)
    ident = pk[0][0]
    sr = struct.unpack_from("<I", ident, 12)[0]
    bs = (1 << (ident[28] & 15), 1 << (ident[28] >> 4))
    total = pk[-1][1]
    pg = oggpk.pages(b)
    serial = pg[0][2]
    head = oggpk.replace_comment(b, pk[1][0], serial)
    # the header pages of replace_comment's output
    hp = oggpk.pages(head); n = 0; k = 0
    for h in hp:
        k += 1; n += sum(1 for x in h[4] if x < 255)
        if n >= 3: break
    hdr = b"".join(oggpk.page(*h) for h in hp[:k])
    seq = k
    body = b""
    for flags, gran, lace, data in audio_pages([p for p, _ in pk[3:]], bs, total, sr, initial_padding):
        body += oggpk.page(flags, gran, serial, seq, lace, data); seq += 1
    return hdr + body

def main():
    d = sys.argv[1]; lim = int(sys.argv[2]) if len(sys.argv) > 2 else 10**9
    fs = sorted(f for f in os.listdir(d) if f.endswith(".ogg"))[:lim]
    for ip in (int(x) for x in os.environ.get("PADS", "128").split(",")):
        same = diff = 0; first = None
        for f in fs:
            b = open(os.path.join(d, f), "rb").read()
            try: r = rebuild(b, ip)
            except Exception as e: r = None
            if r == b: same += 1
            else:
                diff += 1
                if first is None: first = f
        print("initial_padding %d: %d same, %d differ (first %s)" % (ip, same, diff, first))

if __name__ == "__main__":
    main()
