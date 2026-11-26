"""banks.py (romimport, 2026-11-18): a game's music bank as its sound start-up leaves it in RAM, made from the ROM.

gen/mkcontent.py reads No Mercy Plus V3's and WrestleMania 2000's music banks from RAM (the rig's nmbank.pkl, made from
nm_pick_ram.bin; wm2k_ram.bin at *0x800571EC): the bank header (sound scripts) and its instrument bank ("ctl"), both
read from the ROM into the heap and turned from offsets into pointers by the game's own code (exe lifted.cpp):
  set_up_sound_effect_bank (0x80027CA0): header +0xC = 1, +0x14 and each entry's +0x18 (8 bytes apart, +0 of them) + B;
  then the header's +0x10 = the ctl's address (set_bank_wave_data's store, as the captures hold it);
  relocate_sample_bank (0x80029244): ctl +0x10 |= 0x80000000; the 3 words at +0x24 and the +0x20 words at *(+0x2C) + C
  where non-zero (func_80029774); each instrument's tuning byte (*(+0x28) + 4k) -> f32 (s8 / 100.0 + (s8)(byte at
  *(+0x24) + k) - 0x30)); each wave record (*(*(+0x2C) + 4k)) not yet marked (+9): +0 + the samples' ROM address unless
  0xFF......, +9 = 1, +0xC + C if non-zero, +0x10 + C if byte +8 is 0.
Where in RAM (B, C) is the rig session's heap -- our numbers, kept so the pointer words the content index copies come out
the same; the bytes are the ROM's. Checked against the captures' banks word for word (check_against)."""
import struct
import numpy as np

# game -> (header ROM offset, ctl ROM offset, samples ROM offset, B, C)
BANKS = {
    "nmplus_v3": (0x2E73826, 0x2E74E56, 0x2E77B56, 0x807BFB80, 0x807C11C0),
    "wm2000":    (0x143FD70, 0x14407C0, 0x1442E20, 0x80362B50, 0x803635B0),
    "baddblood": (0x140045C, 0x1400EAC, 0x140350C, 0x80362B50, 0x803635B0),   # (its select screen, imports2 vsel_ram: checked word for word)
}
TUNE_DIV = 100.0           # (the double at 0x80055F08 in No Mercy; the same value in both games' banks: checked)

def r32(m, a): return struct.unpack_from(">I", m, a & 0x7FFFFF)[0]
def w32(m, a, v): struct.pack_into(">I", m, a & 0x7FFFFF, v & 0xFFFFFFFF)

def place(img, rom, game):
    """puts the game's bank into img (a bytearray, 8 MB) as the game's start-up leaves it; -> (B, C)"""
    hdr, ctl, smp, B, C = BANKS[game]
    hl, cl = ctl - hdr, smp - ctl
    img[B & 0x7FFFFF:(B & 0x7FFFFF) + hl] = rom[hdr:hdr + hl]
    img[C & 0x7FFFFF:(C & 0x7FFFFF) + cl] = rom[ctl:ctl + cl]
    # set_up_sound_effect_bank
    w32(img, B + 0xC, 1); w32(img, B + 0x10, C)
    w32(img, B + 0x14, r32(img, B + 0x14) + B)
    for i in range(struct.unpack(">i", img[B & 0x7FFFFF:(B & 0x7FFFFF) + 4])[0]):
        w32(img, B + 8 * i + 0x18, r32(img, B + 8 * i + 0x18) + B)
    # relocate_sample_bank
    w32(img, C + 0x10, r32(img, C + 0x10) | 0x80000000)
    def add_nonzero(at, n):
        for k in range(n):
            v = r32(img, at + 4 * k)
            if v: w32(img, at + 4 * k, v + C)
    add_nonzero(C + 0x24, 3)
    add_nonzero(r32(img, C + 0x2C), r32(img, C + 0x20))
    n = r32(img, C + 0x20)
    for k in range(n):
        ta = r32(img, C + 0x28) + 4 * k
        fine = struct.unpack(">b", bytes([img[ta & 0x7FFFFF]]))[0]
        f = np.float32(np.float64(np.float32(fine)) / TUNE_DIV)
        coarse = ((img[(r32(img, C + 0x24) + k) & 0x7FFFFF] - 0x30) & 0xFF)
        coarse = coarse - 0x100 if coarse & 0x80 else coarse
        f = np.float32(f + np.float32(coarse))
        img[ta & 0x7FFFFF:(ta & 0x7FFFFF) + 4] = struct.pack(">f", float(f))
        e = r32(img, r32(img, C + 0x2C) + 4 * k)
        if img[(e + 9) & 0x7FFFFF] == 0:
            v = r32(img, e)
            if v & 0xFF000000 != 0xFF000000: w32(img, e, v + smp)
            img[(e + 9) & 0x7FFFFF] = 1
            if r32(img, e + 0xC): w32(img, e + 0xC, r32(img, e + 0xC) + C)
            if img[(e + 8) & 0x7FFFFF] == 0: w32(img, e + 0x10, r32(img, e + 0x10) + C)
    return B, C

def check_against(img, cap, game):
    """words of the bank's header and ctl that differ from a capture's"""
    hdr, ctl, smp, B, C = BANKS[game]
    out = []
    for a, n in ((B, ctl - hdr), (C, smp - ctl)):
        for o in range(0, n & ~3, 4):
            x = (a + o) & 0x7FFFFF
            if img[x:x + 4] != cap[x:x + 4]: out.append(a + o)
    return out
