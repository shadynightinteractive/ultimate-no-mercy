#!/usr/bin/env python3
"""replay (2026-11-14): two runs' per-retrace hashes (nm_replay.cpp NM_REPLAY_HASH) compared.

  python replay_diff.py <a.csv> <b.csv> [--fields <fields.json>] [--quiet]

Says whether the two logs agree on every retrace both have, and if not: the first retrace that differs, which
structures differ there (and the first retrace each structure differs at), which members (from <log>.members.bin), and,
when both runs kept that retrace's bytes (NM_REPLAY_DUMP), the offsets that differ, named by the member's record kind
and fields.json ("Wrestler[1] +0x09E animation"). Exit code 0: the same; 1: a difference; 2: not comparable.
"""
import json, os, struct, sys


INFO = 5       # (retrace, pictures, pad_reads, host_calls, host: the columns before the structures)


def read_log(path):
    head, cols, rows = [], None, {}
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if line.startswith("#"):
            head.append(line)
            continue
        if cols is None:
            cols = line.split(",")
            continue
        v = line.split(",")
        if len(v) != len(cols):
            continue        # (a line cut short as the process ended)
        rows[int(v[0])] = v
    return head, cols or [], rows


def read_members(path):
    """[(index, structure, name, address, end, kind, element size)], and [(retrace, hashes)] in file order"""
    members = []
    txt = path + ".members.txt"
    if os.path.exists(txt):
        for line in open(txt, encoding="utf-8"):
            if line.startswith("#"):
                continue
            f = line.rstrip("\n").split("\t")
            members.append((int(f[0]), f[1], f[2], int(f[3], 16), int(f[4], 16), f[5], int(f[6])))
    records = []
    binp = path + ".members.bin"
    if os.path.exists(binp):
        data = open(binp, "rb").read()
        if len(data) >= 12:
            magic, version, n = struct.unpack_from("<III", data, 0)
            at, size = 12, 4 + 8 * n
            while at + size <= len(data):
                r = struct.unpack_from("<I", data, at)[0]
                records.append((r, struct.unpack_from("<%dQ" % n, data, at + 4)))
                at += size
    return members, records


def members_at(records, retrace):
    """the members' hashes as they stood at `retrace` (each record is written when one changed)"""
    best = None
    for r, h in records:
        if r > retrace:
            break
        best = h
    return best


def field_name(fields, kind, off):
    k = fields.get(kind) if fields else None
    if not k:
        return ""
    best, name = -1, ""
    for o, f in (k.get("fields") or {}).items():
        o = int(o, 16)
        if best < o <= off:
            best, name = o, f.get("name", "")
    if best < 0:
        return ""
    return name if best == off else "%s+0x%X" % (name, off - best)


def main(argv):
    quiet = "--quiet" in argv
    fields = None
    if "--fields" in argv:
        fields = json.load(open(argv[argv.index("--fields") + 1], encoding="utf-8"))
    pos, skip = [], False
    for x in argv[1:]:
        if skip:
            skip = False
        elif x == "--fields":
            skip = True
        elif not x.startswith("--"):
            pos.append(x)
    a_path, b_path = pos[:2]
    ha, ca, ra = read_log(a_path)
    hb, cb, rb = read_log(b_path)
    if ca != cb:
        print("NOT COMPARABLE: the logs hash different structures\n  %s\n  %s" % (",".join(ca), ",".join(cb)))
        return 2
    if ha[:1] != hb[:1]:
        print("NOT COMPARABLE: made from different tables\n  %s\n  %s" % (ha[:1], hb[:1]))
        return 2
    both = sorted(set(ra) & set(rb))
    if not both:
        print("NOT COMPARABLE: no retrace in both")
        return 2
    first = {}
    for r in both:
        for k in range(INFO, len(ca)):
            if ra[r][k] != rb[r][k] and ca[k] not in first:
                first[ca[k]] = r
    only_a, only_b = len(set(ra) - set(rb)), len(set(rb) - set(ra))
    print("%s: %d retraces\n%s: %d retraces\ncompared: %d (retraces %d..%d), structures %s" % (
        a_path, len(ra), b_path, len(rb), len(both), both[0], both[-1], ", ".join(ca[INFO:])))
    if only_a or only_b:
        print("  (in one only: %d of a's, %d of b's -- the runs ended at different retraces)" % (only_a, only_b))
    # the game's own counts, which a replay that went another way usually shows first
    pic = next((r for r in both if ra[r][1] != rb[r][1]), None)
    pads = next((r for r in both if ra[r][2] != rb[r][2]), None)
    host = next((r for r in both if ra[r][3:5] != rb[r][3:5]), None)
    if not first:
        print("THE SAME: every structure's hash agrees at every retrace both have%s" % (
            "" if pic is None and pads is None and host is None else
            " (but the counts differ: pictures from %s, pad reads from %s, the host's answers from %s)" % (pic, pads, host)))
        return 0
    r0 = min(first.values())
    print("DIFFERENT: first at retrace %d, in %s" % (r0, ", ".join(s for s, r in sorted(first.items(), key=lambda x: x[1]) if r == r0)))
    for s, r in sorted(first.items(), key=lambda x: x[1]):
        print("  %-18s first differs at retrace %d" % (s, r))
    if pic is not None or pads is not None or host is not None:
        print("  (the pictures made first differ at retrace %s, the pad reads at %s, the host's answers to the mods at %s)" % (pic, pads, host))
    ma, reca = read_members(a_path)
    mb, recb = read_members(b_path)
    if ma and ma == mb and reca and recb:
        A, B = members_at(reca, r0), members_at(recb, r0)
        if A and B:
            diff = [i for i in range(len(ma)) if A[i] != B[i]]
            print("members differing at retrace %d: %d" % (r0, len(diff)))
            for i in diff[:40]:
                m = ma[i]
                print("  %-16s %-60s 0x%08X-0x%08X" % (m[1], m[2][:60], m[3], m[4]))
            # the bytes, when both runs kept them at that retrace
            da, db = "%s.dump_%d.bin" % (a_path, r0), "%s.dump_%d.bin" % (b_path, r0)
            if os.path.exists(da) and os.path.exists(db):
                xa, xb = open(da, "rb").read(), open(db, "rb").read()
                at = 0
                shown = 0
                for m in ma:
                    n = m[4] - m[3]
                    for off in range(n):
                        if at + off < len(xa) and at + off < len(xb) and xa[at + off] != xb[at + off] and shown < 40:
                            where = ""
                            if m[5] != "-" and m[6]:
                                where = "%s[%d] +0x%03X %s" % (m[5], off // m[6], off % m[6], field_name(fields, m[5], off % m[6]))
                            print("  byte 0x%08X (%s +0x%X) %s: %02X vs %02X" % (m[3] + off, m[2][:40], off, where, xa[at + off], xb[at + off]))
                            shown += 1
                    at += n
            elif not quiet:
                print("  (NM_REPLAY_DUMP=%d in both runs keeps the bytes there: the offsets and field names)" % r0)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
