# The Revenge ports' second IDs ("banks"): a man's looks past the four costumes a Superstar has (data\revport\attires\
# ATTIRES.md). His four costumes are WCW/nWo Revenge's own four (its costume table); a look he had before that Revenge
# doesn't give him (a mask or colours of his own put together otherwise) is kept as a costume of a second ID of his --
# the same man: his LATE entry (gen/mkroster.py: name, moves, abilities, music), his photo, body and commentary names
# copied, a new ID. On the Superstar Select it is a costume variant in his box (src/variants.c: its row's n, the
# costumes the Type arrows show after his four); its own box is off the pages, and it follows his roster option
# (src/logic.c arcade_roster_opt). Read by mkroster, wmport, mkparts, mkovr, mkcontent, names.
BANKS = [
    # key, base key, base ID, its ID, its costumes (capture, slot) -- wmport REV_PORTED; the looks
    dict(key="brethart_b", base="brethart", base_id=0x060D, id=0x060F, caps=[("bret_c1", 0)],
         about="his black and pink with his shades"),
    dict(key="savage_b", base="savage", base_id=0x0108, id=0x0115, caps=[("sv_c3", 0)],
         about="the red tights with the black MADNESS bandana, no shades"),
    dict(key="scottsteiner_b", base="scottsteiner", base_id=0x010F, id=0x0116, caps=[("st_c2", 1)],
         about="the black singlet with his shades"),
    dict(key="booker_b", base="booker", base_id=0x0110, id=0x0117, caps=[("hz_c1", 0)],
         about="the flames tights in red"),
    dict(key="stevieray_b", base="stevieray", base_id=0x0111, id=0x0118, caps=[("hz_c1", 1)],
         about="the flames tights in red"),
    dict(key="rey_b", base="rey", base_id=0x0112, id=0x0119, caps=[("rb_c1", 0)],
         about="the purple bodysuit with the red mask"),
]
BY = {b["key"]: b for b in BANKS}
