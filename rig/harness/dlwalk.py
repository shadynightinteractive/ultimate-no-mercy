"""F3DEX2 display list walker over an N64 RAM image: follows G_DL calls/branches and reports commands
whose addresses fall outside RAM (or are 0), and triangles naming vertices not loaded.
    walk(ram, dl_addr) -> (issues, stats)
"""
import struct
OPN = {0x01: "VTX", 0xDA: "MTX", 0xDC: "MOVEMEM", 0xFD: "SETTIMG", 0xDE: "DL", 0xDB: "MOVEWORD", 0xD8: "POPMTX"}
def walk(ram, start, max_cmds=400000, ram_size=0x800000):
    seg = [0] * 16
    issues = []; stats = {}
    def phys(a):
        s = (a >> 24) & 0x0F if (a >> 24) < 0x10 else None
        if s is not None: return (seg[s] + (a & 0xFFFFFF)) & 0x1FFFFFFF
        return a & 0x1FFFFFFF
    stack = []; pc = phys(start); n = 0
    vtx_loaded = [False] * 64
    while n < max_cmds:
        n += 1
        if pc + 8 > ram_size or pc == 0: issues.append(("pc out of RAM", hex(pc))); break
        w0, w1 = struct.unpack_from(">II", ram, pc)
        op = w0 >> 24
        stats[op] = stats.get(op, 0) + 1
        here = pc; pc += 8
        if op in (0x01, 0xDA, 0xDC, 0xFD, 0xDE):
            p = phys(w1)
            if p == 0 or p >= ram_size: issues.append((OPN[op], hex(here), hex(w0), hex(w1), hex(p)))
        if op == 0x01:
            nv = (w0 >> 12) & 0xFF; end = (w0 >> 1) & 0x7F
            for i in range(end - nv, end):
                if 0 <= i < 64: vtx_loaded[i] = True
        elif op in (0x05, 0x06, 0x07):
            idx = [((w0 >> 16) & 0xFF) // 2, ((w0 >> 8) & 0xFF) // 2, (w0 & 0xFF) // 2]
            if op != 0x05: idx += [((w1 >> 16) & 0xFF) // 2, ((w1 >> 8) & 0xFF) // 2, (w1 & 0xFF) // 2]
            for i in idx:
                if i >= 64: issues.append(("tri index", hex(here), hex(w0), hex(w1))); break
        elif op == 0xDB:
            if (w0 >> 16) & 0xFF == 0x06: seg[((w0 & 0xFFFF) // 4) & 15] = w1 & 0x1FFFFFFF
        elif op == 0xDE:
            if not (w0 >> 16) & 0xFF:
                if len(stack) > 18: issues.append(("DL stack overflow", hex(here))); break
                stack.append(pc)
            pc = phys(w1)
        elif op == 0xDF:
            if not stack: break
            pc = stack.pop()
    return issues, stats, n
