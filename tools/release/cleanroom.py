#!/usr/bin/env python3
"""cleanroom.py: refuses a package folder that holds any data taken from a game ROM.

  python cleanroom.py scan <package folder or archive> --rom <ROM> [--rom ...] [--forbid <file or folder> ...]
                      [--manifest <provenance.json>] [--report <out.txt>] [--json <out.json>]
  python cleanroom.py manifest-check <package folder> <provenance.json>

(2026-11-15, the user's rule of 2026-11-14: "No game data shipped, rom(s) needed.")

How it looks. Every 32-byte window of every ROM given (--rom; .z64/.v64/.n64 or a .zip holding one, in any byte order: all
are turned to the ROM's own order first) and of every forbidden file given (--forbid: the files the game makes from ROMs on
first run -- content\*.bin, the wrestler folders, ... -- and any other data that must never ship, such as the commentary
pack) is hashed, at every 4th byte. Then every window of every file in the package, at every byte, and again with its bytes
swapped in pairs and in fours (a .v64 or .n64 copy hidden inside), is looked up. A hit is checked byte for byte against the
source, so a hash collision never refuses a package. Any run of 35 or more bytes the package shares with a ROM or a
forbidden file is found (a run that long always holds a ROM window starting on a multiple of 4).

Windows with almost nothing in them -- the same byte, or a pattern of 2, 3, 4, 6, 8, 12 or 16 bytes, over and over (zeros,
0xFF fill, a repeated word or colour), or with fewer than 6 different byte values in it, or counting (bytes going up by one step, alone or interleaved 2, 4 or 8\nways: an index table) -- are left out on both sides: every program has them and they say nothing about where data came from.

Archives in the package: .zip (and any file that is a zip by its first bytes: a recomp mod, .nrm) and .tar(.gz/.xz/.bz2)
are opened and each member scanned; any other archive or compressed
file (.7z, .rar, ...) is refused, since it could not be looked into. Compressed or encrypted data inside an ordinary file
cannot be seen by any byte scanner: that is what the provenance manifest is for (manifest-check: every file in the package
must be listed in it with what it is and what it was made from, and its size and SHA-256 must match).

Exit code: 0 clean, 1 refused (a hit, an unlisted file or an unreadable archive), 2 bad usage.
Needs numpy. Memory: about 12 bytes for every 4 bytes of ROM and forbidden data, plus the sources themselves.
"""
import argparse, hashlib, io, json, os, sys, tarfile, zipfile
import numpy as np

W = 32                 # window length
STRIDE = 4             # source windows at every 4th byte
CHUNK = 8 << 20        # bytes hashed at a time (keeps memory down)
B = np.uint64(0x100000001B3)          # the polynomial's base (FNV's prime; any odd number works)
M1 = np.uint64(0xBF58476D1CE4E5B9)    # splitmix64's mixer, so the low bits depend on every byte
M2 = np.uint64(0x94D049BB133111EB)
ROM_EXT = ('.z64', '.v64', '.n64', '.rom', '.bin')
OPENABLE = ('.zip',)
TAR_EXT = ('.tar', '.tar.gz', '.tgz', '.tar.xz', '.txz', '.tar.bz2')
CLOSED_ARCHIVES = ('.7z', '.rar', '.gz', '.xz', '.bz2', '.lzma', '.zst', '.cab', '.lz4', '.arj')

np.seterr(over='ignore')


# ---------------------------------------------------------------- ROMs in any byte order
def rom_order(b):
    """b in the ROM's own (big-endian, .z64) order, whatever order the file was in; None if it is no N64 ROM"""
    if len(b) < 0x40: return None
    h = b[:4]
    if h == b'\x80\x37\x12\x40': return b
    if h == b'\x37\x80\x40\x12': return swap(b, 2)
    if h == b'\x40\x12\x37\x80': return swap(b, 4)
    return None

def swap(b, n, align=0):
    """b with each group of n bytes (from offset align) reversed; the bytes before align and the tail are kept"""
    a = np.frombuffer(b, np.uint8).copy()
    m = (len(a) - align) // n * n
    if m > 0:
        v = a[align:align + m].reshape(-1, n)
        a[align:align + m] = v[:, ::-1].reshape(-1)
    return a.tobytes()

def load_roms(path):
    """[(name, bytes)] for a ROM file or a .zip holding ROMs"""
    out = []
    low = path.lower()
    if low.endswith('.zip'):
        with zipfile.ZipFile(path) as z:
            for m in z.namelist():
                d = z.read(m); r = rom_order(d)
                if r is not None: out.append((os.path.basename(path) + ':' + m, r))
        return out
    d = open(path, 'rb').read(); r = rom_order(d)
    if r is None: raise SystemExit('cleanroom: %s is not an N64 ROM (.z64/.v64/.n64, or a .zip of one)' % path)
    return [(os.path.basename(path), r)]


# ---------------------------------------------------------------- hashing
def window_hashes(a):
    """the hash of every 32-byte window of a (uint8 array), at every byte: len(a) - 31 values; and which are trivial.
    A window is read as four 64-bit words (unaligned views of the same bytes), each mixed in turn (splitmix64-style)."""
    n = len(a) - W + 1
    if n <= 0: return np.zeros(0, np.uint64), np.zeros(0, bool)
    a = np.ascontiguousarray(a)
    w = [np.ndarray((n,), dtype='<u8', buffer=a, offset=8 * k, strides=(1,)) for k in range(4)]
    h = np.full(n, 0x9E3779B97F4A7C15, np.uint64)
    for x in w:
        h = (h ^ x) * M1
        h ^= h >> np.uint64(29)
        h *= M2
        h ^= h >> np.uint64(32)
    # trivial: periodic with period 16 (that takes in 1, 2, 4 and 8: zeros, fills, a repeated word) or 12 (3 and 6: a
    # repeated RGB colour)
    trivial = (w[0] == w[2]) & (w[1] == w[3])
    eq = (a[:-12] == a[12:]).astype(np.int32)
    c = np.concatenate(([0], np.cumsum(eq)))
    trivial |= (c[W - 12:W - 12 + n] - c[:n]) == (W - 12)
    # and fewer than MIN_DISTINCT different byte values in it (mostly zeros with a few small numbers: a header, a table of
    # flags) -- such windows turn up by chance in any program (an exe's DOS header matched a ROM's zero-filled table)
    trivial |= distinct_counts(a, n) < MIN_DISTINCT
    # and counting: bytes going up (or down) by the same step, alone or interleaved two, four or eight ways (0 1 2 3 ...,
    # 0D 03 0D 04 0D 05 ..., a table of 16- or 32-bit counters) -- the second difference at that stride is zero all along
    # (the DLLs' 0 1 2 .. 2F tables matched the ROM's before this). A periodic window is one of these too.
    for s in (1, 2, 4, 8):
        m = W - 2 * s
        d2 = (a[2 * s:] + a[:-2 * s] - a[s:-s] - a[s:-s]) != 0          # (uint8: mod 256)
        c = np.concatenate(([0], np.cumsum(d2.astype(np.int32))))
        trivial |= (c[m:m + n] - c[:n]) == 0
        # (and, byte by byte, a window with fewer than 8 places where it doesn't go on as before -- zeros, then a count, then
        #  a word: what a padded table or a short string in zeros looks like; real data turns at nearly every byte. The same at
        #  strides 2 and 4: a table of 16- or 32-bit numbers)
        if s <= 4: trivial |= (c[m:m + n] - c[:n]) < MIN_TURNS
    return h, trivial

MIN_TURNS = 8

MIN_DISTINCT = 6

def distinct_counts(a, n):
    """how many different byte values each of the n 32-byte windows of a holds (a 256-bit set per byte, OR-ed over the
    window by doubling, counted), a part at a time to keep memory down"""
    out = np.empty(n, np.int32)
    step = 1 << 21
    for s in range(0, n, step):
        part = a[s:min(n, s + step) + W - 1]
        hi = (part >> 6).astype(np.uint8); bit = np.left_shift(np.uint64(1), (part & 63).astype(np.uint64))
        tot = None
        for j in range(4):
            x = np.where(hi == j, bit, np.uint64(0))
            for d in (1, 2, 4, 8, 16):
                x = x[:-d] | x[d:]
            c = np.bitwise_count(x).astype(np.int32)
            tot = c if tot is None else tot + c
        out[s:s + len(tot)] = tot
    return out
def chunked(b, stride):
    """(offsets, hashes) of b's non-trivial windows, at every stride-th byte, a chunk at a time"""
    a = np.frombuffer(b, np.uint8)
    offs, hs = [], []
    for s in range(0, max(len(a) - W + 1, 0), CHUNK):
        part = a[s:s + CHUNK + W - 1]
        h, t = window_hashes(part)
        idx = np.arange(0, len(h), dtype=np.int64)
        keep = ~t
        if stride > 1:
            keep &= ((idx + s) % stride) == 0
        offs.append(idx[keep] + s); hs.append(h[keep])
    if not hs: return np.zeros(0, np.int64), np.zeros(0, np.uint64)
    return np.concatenate(offs), np.concatenate(hs)


class Source:
    """a ROM, or a set of forbidden files kept as one (one look-up for all of them): the bytes, and the windows' hashes sorted
    with where each is. Windows across two files' join are left out."""
    def __init__(self, name, parts, kind):
        self.name, self.kind = name, kind
        self.data = b''.join(d for _, d in parts)
        self.names = [n for n, _ in parts]
        self.starts = np.cumsum([0] + [len(d) for _, d in parts])[:-1].astype(np.int64)
        offs, hs = [], []
        for st, (_, d) in zip(self.starts, parts):
            if len(d) < W: continue
            off, h = chunked(d, STRIDE)
            offs.append(off + st); hs.append(h)
        h = np.concatenate(hs) if hs else np.zeros(0, np.uint64)
        off = np.concatenate(offs) if offs else np.zeros(0, np.int64)
        order = np.argsort(h, kind='stable')
        self.h = h[order]; self.off = off[order].astype(np.int64)
    def where(self, so):
        """a source offset as '<file> at 0x..'"""
        i = int(np.searchsorted(self.starts, so, side='right')) - 1
        return '%s at 0x%X' % (self.names[i], so - int(self.starts[i]))
    def lookup(self, qh):
        """for each query hash: the index into self.h of an equal hash, or -1"""
        if len(self.h) == 0: return np.full(len(qh), -1, np.int64)
        i = np.searchsorted(self.h, qh)
        i = np.minimum(i, len(self.h) - 1)
        return np.where(self.h[i] == qh, i, -1)


# ---------------------------------------------------------------- one file of the package
class Corpus:
    """every source's windows in one sorted table (one look-up a chunk, however many sources), then each source's own
    arrays dropped: the bytes stay, for the byte-for-byte check of a hit.

    (relcheck 2026-11-19, approved by the user on 2026-11-19: "I looked at your list you needed from me and I concur with
    your recommendations"; independent review: notes REVIEW_RELCHECK.txt) The table keeps ONE entry per (window hash, source)
    -- that source's first place with the window -- and a look-up gives EVERY source a window is in (lookup_all), not just
    the first one in the table's order. A report used to name one source per window: whichever ROM was given first, so the
    same file got a different verdict from a different set or order of ROMs (REVIEW_ALLOWANCES.txt finding 0)."""
    def __init__(self, sources):
        self.sources = sources
        hs = [s.h for s in sources]; n = sum(len(h) for h in hs)
        # (relcheck: offsets as 32-bit numbers when every source is under 2 GB -- the table is the audit's biggest allocation,
        #  and an audit died of a MemoryError while other programs held most of the PC's memory)
        odt = np.int32 if all(len(s.data) < 2 ** 31 for s in sources) else np.int64
        self.h = np.empty(n, np.uint64); self.src = np.empty(n, np.uint16); self.off = np.empty(n, odt)
        p = 0
        for i, s in enumerate(sources):
            k = len(s.h); self.h[p:p + k] = s.h; self.src[p:p + k] = i; self.off[p:p + k] = s.off; p += k
            s.h = s.off = None
        order = np.argsort(self.h, kind='stable')
        self.h = self.h[order]; self.src = self.src[order]; self.off = self.off[order]
        del order
        # (a stable sort of the sources laid end to end: equal hashes stay grouped by source, in source order, so one entry
        #  per (hash, source) is the first of each run)
        if n:
            keep = np.ones(n, bool)
            keep[1:] = (self.h[1:] != self.h[:-1]) | (self.src[1:] != self.src[:-1])
            self.h = self.h[keep]; self.src = self.src[keep]; self.off = self.off[keep]
            del keep
    def lookup(self, qh):
        """for each query hash: the index of the first entry with that hash (its first source), or -1"""
        if len(self.h) == 0: return np.full(len(qh), -1, np.int64)
        i = np.minimum(np.searchsorted(self.h, qh), len(self.h) - 1)
        return np.where(self.h[i] == qh, i, -1)
    def lookup_all(self, qh):
        """for each query hash: the range [lo, hi) of the table's entries with that hash -- one per source that has it"""
        if len(self.h) == 0: z = np.zeros(len(qh), np.int64); return z, z
        return (np.searchsorted(self.h, qh, side='left').astype(np.int64),
                np.searchsorted(self.h, qh, side='right').astype(np.int64))

def covers(o, n, al, size):
    """(start, end): the bytes of the file a window at o of the copy swapped in groups of n from al is made of"""
    if n == 1: return o, o + W
    m = (size - al) // n * n                 # (swap(): groups from al, the bytes before al and the tail kept as they are)
    g = lambda x: x - (x - al) % n if al <= x < al + m else x
    last = o + W - 1
    return g(o), (g(last) + n if al <= last < al + m else last + 1)

MAX_HITS = 200000      # (windows whose place and sources are kept for the caller; beyond this only the counts: 'hits_cut')

def scan_bytes(name, b, corpus, max_examples=8, swaps=True):
    """the verified hits of b against the corpus: {covered, windows, by_source, sets, hits, examples}.
    (relcheck 2026-11-19) Every source a window is in counts: by_source = how many windows each source has (a window in three
    ROMs counts for each); sets = how many windows have each exact set of sources ('A + B'); hits = [(offset in b, start,
    end, [its sources])] for each window -- start..end are the bytes of b it is made of (a window found in a swapped copy
    takes in the whole groups it starts and ends in) -- at most MAX_HITS ('hits_cut' if there were more)."""
    res = {'file': name, 'size': len(b), 'windows': 0, 'covered': 0, 'by_source': {}, 'sets': {}, 'hits': [],
           'hits_cut': False, 'examples': []}
    if len(b) < W: return res
    cover = np.zeros(len(b) + 1, np.int32)
    variants = [('as is', 1, 0)]
    if swaps:
        variants += [('pairs swapped from %d' % al, 2, al) for al in (0, 1)]
        variants += [('fours swapped from %d' % al, 4, al) for al in (0, 1, 2, 3)]
    for vname, n, al in variants:
        vb = b if n == 1 else swap(b, n, al)
        a = np.frombuffer(vb, np.uint8)
        for s in range(0, len(a) - W + 1, CHUNK):          # a chunk at a time: memory stays small for a big file
            h, t = window_hashes(a[s:s + CHUNK + W - 1])
            off = np.nonzero(~t)[0].astype(np.int64); h = h[off]; off += s
            lo, hi = corpus.lookup_all(h)
            hit = np.nonzero(hi > lo)[0]
            if len(hit) == 0: continue
            # every (window, source) pair: the window's hit index k and the table entry j (one per source that has the hash)
            cnt = hi[hit] - lo[hit]
            pk = np.repeat(hit, cnt)
            pj = np.repeat(lo[hit], cnt) + (np.arange(int(cnt.sum()), dtype=np.int64) - np.repeat(np.cumsum(cnt) - cnt, cnt))
            # check pairs byte for byte (a hash is not proof): up to 4096 of them, spread over the chunk; if one of those
            # is a collision (64-bit hashes: not expected), every pair is checked, and a collision just drops its pair
            def same(q):
                fo = int(off[pk[q]]); j = int(pj[q]); src = corpus.sources[int(corpus.src[j])]; so = int(corpus.off[j])
                return vb[fo:fo + W] == src.data[so:so + W]
            npair = len(pk)
            sample = np.arange(npair) if npair <= 4096 else np.linspace(0, npair - 1, 4096).astype(np.int64)
            ok = np.arange(npair) if all(same(q) for q in sample) else np.array([q for q in range(npair) if same(q)], np.int64)
            if len(ok) == 0: continue
            pk, pj = pk[ok], pj[ok]
            wins = np.unique(pk)                  # the windows with at least one verified source
            fos = off[wins]
            # a window found in a swapped copy covers the same bytes of the file (swapping keeps bytes in their group)
            lo_c = np.maximum(fos - (n - 1), 0); hi_c = np.minimum(fos + W + (n - 1), len(b))
            np.add.at(cover, lo_c, 1); np.add.at(cover, hi_c, -1)
            res['windows'] += len(wins)
            sids, c = np.unique(corpus.src[pj], return_counts=True)     # (pairs are unique per source: one entry each)
            for si, k in zip(sids.tolist(), c.tolist()):
                nm = corpus.sources[si].name; res['by_source'][nm] = res['by_source'].get(nm, 0) + k
            # each window's set of sources (pk is sorted: runs of the same window)
            starts = np.concatenate(([0], np.nonzero(np.diff(pk))[0] + 1, [len(pk)]))
            for x in range(len(starts) - 1):
                q0, q1 = int(starts[x]), int(starts[x + 1])
                names = sorted({corpus.sources[int(corpus.src[j])].name for j in pj[q0:q1].tolist()})
                key = ' + '.join(names); res['sets'][key] = res['sets'].get(key, 0) + 1
                if len(res['hits']) < MAX_HITS: res['hits'].append((int(off[pk[q0]]),) + covers(int(off[pk[q0]]), n, al, len(b)) + (names,))
                else: res['hits_cut'] = True
                if len(res['examples']) < max_examples:
                    res['examples'].append('%s at 0x%X = %s (%s)' % (name, int(off[pk[q0]]), '; '.join(
                        corpus.sources[int(corpus.src[j])].where(int(corpus.off[j])) for j in pj[q0:q1].tolist()), vname))
    res['covered'] = int((np.cumsum(cover)[:len(b)] > 0).sum())
    return res
MAGIC_CLOSED = (b'\x1f\x8b', b'\xfd7zXZ\x00', b"7z\xbc\xaf'\x1c", b'(\xb5/\xfd', b'BZh', b'Rar!\x1a\x07', b'MSCF')

def is_zip(path):
    """a zip by its first bytes, whatever its name (a recomp mod, .nrm, is a zip)"""
    with open(path, 'rb') as f: return f.read(4) == b'PK\x03\x04'

def members(path, rel):
    """(name, bytes) of every file inside a package file: itself, or an archive's members; ('!', why) when it can't be read"""
    low = path.lower()
    if low.endswith(OPENABLE) or (not low.endswith(TAR_EXT) and is_zip(path)):
        try:
            with zipfile.ZipFile(path) as z:
                for m in z.infolist():
                    if not m.is_dir(): yield from inner(rel + '!' + m.filename, z.read(m))
        except zipfile.BadZipFile as e: yield ('!', rel + ': unreadable zip (%s)' % e)
        return
    if low.endswith(TAR_EXT):
        try:
            with tarfile.open(path) as t:
                for m in t.getmembers():
                    if m.isfile(): yield from inner(rel + '!' + m.name, t.extractfile(m).read())
        except tarfile.TarError as e: yield ('!', rel + ': unreadable tar (%s)' % e)
        return
    if low.endswith(CLOSED_ARCHIVES):
        yield ('!', rel + ': a compressed file or archive this scanner cannot open: not allowed in a package')
        return
    data = open(path, 'rb').read()
    if data.startswith(MAGIC_CLOSED):
        yield ('!', rel + ': compressed data (by its first bytes) this scanner cannot open: not allowed in a package')
        return
    yield (rel, data)

def inner(name, data):
    """a member of an archive, opened again if it is itself an archive"""
    low = name.lower()
    if low.endswith(OPENABLE) or data[:4] == b'PK\x03\x04':
        try:
            with zipfile.ZipFile(io.BytesIO(data)) as z:
                for m in z.infolist():
                    if not m.is_dir(): yield from inner(name + '!' + m.filename, z.read(m))
        except zipfile.BadZipFile as e: yield ('!', name + ': unreadable zip (%s)' % e)
    elif (low.endswith(CLOSED_ARCHIVES) and not low.endswith(TAR_EXT)) or data.startswith(MAGIC_CLOSED):
        yield ('!', name + ': a compressed file or archive this scanner cannot open: not allowed in a package')
    else:
        yield (name, data)

def package_files(root):
    if os.path.isfile(root):
        yield root, os.path.basename(root); return
    for d, dirs, fs in os.walk(root):
        dirs.sort()
        for f in sorted(fs):
            p = os.path.join(d, f)
            yield p, os.path.relpath(p, root).replace('\\', '/')


# ---------------------------------------------------------------- provenance manifest
def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for blk in iter(lambda: f.read(1 << 20), b''): h.update(blk)
    return h.hexdigest()

def manifest_check(root, manifest_path):
    """problems: files not listed, listed files missing or changed, entries that say ROM data"""
    man = json.load(open(manifest_path, encoding='utf-8'))
    listed = {e['path']: e for e in man['files']}
    probs = []
    seen = set()
    for p, rel in package_files(root):
        if rel == os.path.basename(manifest_path) and os.path.dirname(os.path.abspath(p)) == os.path.abspath(root): continue
        seen.add(rel)
        e = listed.get(rel)
        if e is None: probs.append('not in the manifest: ' + rel); continue
        if e.get('size') != os.path.getsize(p): probs.append('size differs from the manifest: ' + rel)
        elif e.get('sha256') != sha256(p): probs.append('SHA-256 differs from the manifest: ' + rel)
        if e.get('kind') not in ('program', 'original', 'licence', 'third-party program'):
            probs.append('manifest kind %r is not allowed in a package: %s' % (e.get('kind'), rel))
        if not e.get('from'): probs.append('manifest says nothing of where it came from: ' + rel)
    for rel in listed:
        if rel not in seen: probs.append('in the manifest but not in the package: ' + rel)
    return probs


# ---------------------------------------------------------------- the command
def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd')
    s = sub.add_parser('scan'); s.add_argument('package')
    s.add_argument('--rom', action='append', default=[]); s.add_argument('--forbid', action='append', default=[])
    s.add_argument('--common', action='append', default=[], help='independent public software (a file or folder): a window found in it is a common pattern (an alphabet, ASCII order, a standard table), not counted')
    s.add_argument('--allow', help='reviewed exceptions (a JSON list of {path, sources, max_windows, why, approved}): an approved one that covers a file''s hits makes it ''allowed'', an unapproved one is shown beside the refusal')
    s.add_argument('--manifest'); s.add_argument('--report'); s.add_argument('--json')
    s.add_argument('--quiet', action='store_true')
    s.add_argument('--skip', action='append', default=[], help='a path in the package (relative, / between folders) to leave out, with all under it')
    s.add_argument('--only', action='append', default=[], help='scan only these paths in the package (relative, / between folders; a folder takes all under it)')
    s.add_argument('--no-swaps', action='store_true', help='skip the byte-swapped looks (faster; for an audit, never for a package)')
    m = sub.add_parser('manifest-check'); m.add_argument('package'); m.add_argument('manifest')
    a = ap.parse_args()
    if a.cmd == 'manifest-check':
        probs = manifest_check(a.package, a.manifest)
        for p in probs: print('REFUSED:', p)
        print('manifest: %s' % ('OK' if not probs else '%d problem(s)' % len(probs)))
        sys.exit(1 if probs else 0)
    if a.cmd != 'scan': ap.print_help(); sys.exit(2)
    if not a.rom: print('cleanroom: give at least one --rom'); sys.exit(2)

    sources = []
    for r in a.rom:
        for name, data in load_roms(r):
            sources.append(Source('ROM ' + name, [(name, data)], 'rom'))
            print('indexed ROM %-56s %10d bytes  %9d windows' % (name[:56], len(data), len(sources[-1].h)), flush=True)
    for fpath in a.forbid:
        fpath = fpath.rstrip('/\\')
        if os.path.isfile(fpath): parts = [(os.path.basename(fpath), open(fpath, 'rb').read())]
        else: parts = [(os.path.basename(fpath) + '/' + rel, open(p, 'rb').read()) for p, rel in package_files(fpath)]
        # (a zip among them -- a mod's .nrm -- counts with its members as they are inside, not only as compressed bytes)
        for name, d in list(parts):
            if d[:4] == b'PK\x03\x04':
                try:
                    with zipfile.ZipFile(io.BytesIO(d)) as z:
                        parts += [(name + '!' + m.filename, z.read(m)) for m in z.infolist() if not m.is_dir()]
                except zipfile.BadZipFile: pass
        src = Source('forbidden ' + os.path.basename(fpath), parts, 'forbidden')
        sources.append(src)
        print('indexed forbidden %-50s %4d files %10d bytes  %9d windows' % (os.path.basename(fpath)[:50], len(parts), len(src.data), len(src.h)), flush=True)

    corpus = Corpus(sources)
    allow = json.load(open(a.allow, encoding='utf-8'))['exceptions'] if a.allow else []
    if a.common:
        # (windows also in independent public software are common patterns -- the base64 alphabet, ASCII in order, a
        #  standard table -- and say nothing of a ROM: dropped from the sources, so a package is never refused for them)
        ch = []
        for c in a.common:
            for p in ([c] if os.path.isfile(c) else [q for q, _ in package_files(c)]):
                d = np.frombuffer(open(p, 'rb').read(), np.uint8)
                for s0 in range(0, max(len(d) - W + 1, 0), CHUNK):
                    h, tr = window_hashes(d[s0:s0 + CHUNK + W - 1]); ch.append(np.unique(h[~tr]))
        ch = np.unique(np.concatenate(ch)) if ch else np.zeros(0, np.uint64)
        i = np.minimum(np.searchsorted(ch, corpus.h), max(len(ch) - 1, 0))
        drop = (ch[i] == corpus.h) if len(ch) else np.zeros(len(corpus.h), bool)
        corpus.h, corpus.src, corpus.off = corpus.h[~drop], corpus.src[~drop], corpus.off[~drop]
        print('common patterns: %d source windows dropped (found in the public software given)' % int(drop.sum()), flush=True)
    lines, results, refused = [], [], []
    for p, rel in package_files(a.package):
        if any(rel == k or rel.startswith(k.rstrip('/') + '/') for k in a.skip): continue
        if a.only and not any(rel == k or rel.startswith(k.rstrip('/') + '/') for k in a.only): continue
        for name, data in members(p, rel):
            if name == '!':
                refused.append(data); lines.append('REFUSED  ' + data); continue
            r = scan_bytes(name, data, corpus, swaps=not a.no_swaps)
            results.append(r); n0 = len(lines)
            ex = next((e for e in allow if e['path'] == name and r['windows'] <= e.get('max_windows', 0)
                       and all(any(s.startswith(p) for p in e.get('sources', [''])) for s in r['by_source'])), None)
            if r['windows'] and ex and ex.get('approved'):
                lines.append('allowed  %s: %d windows (%s) -- reviewed: %s' % (name, r['windows'], ', '.join(r['by_source']), ex['why']))
            elif r['windows']:
                refused.append(name)
                if ex: lines.append('         (a proposed exception, NOT approved yet: %s)' % ex['why'])
                lines.append('REFUSED  %s: %d of %d bytes are ROM or forbidden data (%d windows; %s)' % (
                    name, r['covered'], r['size'], r['windows'],
                    ', '.join('%s %d' % kv for kv in sorted(r['by_source'].items(), key=lambda kv: -kv[1])[:4])))
                lines.extend('           ' + e for e in r['examples'])
            elif not a.quiet:
                lines.append('clean    %s (%d bytes)' % (name, r['size']))
            for ln in lines[n0:]: print(ln, flush=True)
    if a.manifest:
        for pr in manifest_check(a.package, a.manifest):
            refused.append(pr); lines.append('REFUSED  ' + pr); print(lines[-1])
    verdict = 'CLEAN: no file holds ROM data or forbidden data (%d files scanned)' % len(results) if not refused else \
              'REFUSED: %d problem(s) in %d files scanned' % (len(refused), len(results))
    lines.append(verdict); print(verdict)
    if a.report: open(a.report, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
    if a.json: json.dump({'verdict': 'clean' if not refused else 'refused', 'results': results, 'problems': refused},
                         open(a.json, 'w', encoding='utf-8'), indent=1)
    sys.exit(1 if refused else 0)

if __name__ == '__main__':
    main()
