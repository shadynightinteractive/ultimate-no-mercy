"""modelf.py: the mod's build read back -- its ELF's sections, symbols and relocations, its link map's input sections
(firstrun, 2026-11-17). Used by mkplan.py. The mod's image (mod_binary.bin) is its ELF's allocated sections laid out from
the link address 0x81000000: a byte at address A is at A - 0x81000000 in the image."""
import re, struct

BASE = 0x81000000


def read_elf(path):
    """(the ELF's bytes, its sections [dict], its symbols [dict]) -- ELF32 big-endian (the mod is MIPS)"""
    b = open(path, 'rb').read()
    assert b[:4] == b'\x7fELF' and b[4] == 1 and b[5] == 2, 'not an ELF32 big-endian file: ' + path
    (shoff,) = struct.unpack_from('>I', b, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from('>HHH', b, 0x2E)
    secs = []
    for i in range(shnum):
        nm, typ, flags, addr, off, size, link, info, align, entsz = struct.unpack_from('>10I', b, shoff + i * shentsize)
        secs.append(dict(i=i, nm=nm, type=typ, flags=flags, addr=addr, off=off, size=size, link=link, info=info))
    def cstr(tab, o):
        return b[tab['off'] + o:b.index(b'\0', tab['off'] + o)].decode('latin-1')
    for s in secs: s['name'] = cstr(secs[shstrndx], s['nm'])
    syms = []
    for s in secs:
        if s['type'] != 2: continue                      # (SHT_SYMTAB)
        for k in range(s['size'] // 16):
            name, value, size, info, other, shndx = struct.unpack_from('>IIIBBH', b, s['off'] + 16 * k)
            if shndx == 0 or shndx >= 0xFF00: continue
            sec = secs[shndx]
            syms.append(dict(name=cstr(secs[s['link']], name), addr=value, size=size, type=info & 15, bind=info >> 4,
                             sec=sec['name'], alloc=bool(sec['flags'] & 2)))
    return b, secs, syms


def reloc_words(b, secs):
    """{address: relocation type} of every relocated field in the linked image (the link's --emit-relocs). A field there holds
    an address the link chose (or, for R_MIPS_26 / HI16 / LO16, an instruction whose immediate does): the link's, not data."""
    out = {}
    for s in secs:
        if s['type'] not in (9, 4): continue             # (SHT_REL, SHT_RELA)
        if not (secs[s['info']]['flags'] & 2): continue
        ent = 8 if s['type'] == 9 else 12
        for k in range(s['size'] // ent):
            off, info = struct.unpack_from('>II', b, s['off'] + ent * k)
            out[off] = info & 0xFF
    return out


def symbols_by_index(b, secs):
    """the symbol table as the relocations number it: [dict(name, addr, size, type, sec index)] (index 0 the null symbol;
    absolute and common symbols have sec None) -- for reloc_entries' symbol numbers"""
    out = []
    for s in secs:
        if s['type'] != 2: continue                      # (SHT_SYMTAB: the one the relocations use)
        for k in range(s['size'] // 16):
            name, value, size, info, other, shndx = struct.unpack_from('>IIIBBH', b, s['off'] + 16 * k)
            tab = secs[s['link']]
            nm = b[tab['off'] + name:b.index(b'\0', tab['off'] + name)].decode('latin-1')
            out.append(dict(name=nm, addr=value, size=size, type=info & 15,
                            sec=None if shndx == 0 or shndx >= 0xFF00 else shndx))
        break
    return out


def reloc_entries(b, secs):
    """[(address, relocation type, symbol index)] of every relocation of an allocated section (the link's --emit-relocs):
    what each relocated field refers to (symbols_by_index)"""
    out = []
    for s in secs:
        if s['type'] not in (9, 4): continue             # (SHT_REL, SHT_RELA)
        if not (secs[s['info']]['flags'] & 2): continue
        ent = 8 if s['type'] == 9 else 12
        for k in range(s['size'] // ent):
            off, info = struct.unpack_from('>II', b, s['off'] + ent * k)
            out.append((off, info & 0xFF, info >> 8))
    return out


def map_inputs(path):
    """[(address, size, object, input section)] of the link map's input sections"""
    out = []
    for line in open(path, encoding='latin-1'):
        m = re.match(r'\s*([0-9a-f]{8})\s+[0-9a-f]{8}\s+([0-9a-f]+)\s+\d+\s+(\S+\.o):\((\S+)\)', line)
        if m: out.append((int(m.group(1), 16), int(m.group(2), 16), re.split(r'[\\/]', m.group(3))[-1], m.group(4)))
    return out
