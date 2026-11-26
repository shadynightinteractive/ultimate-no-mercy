#!/usr/bin/env python3
"""nrm_reproducible.py (2026-11-17, the user's decision: reproducible builds): rewrites a built .nrm (a zip) so the same mod
builds the same file byte for byte. RecompModTool stamps each zip entry with the time it was written; here every entry gets
the zip format's first date (1980-01-01 00:00), the members keep their order and bytes, each deflated again the same way.
The mod loader and tools\\aot hash the members, not the zip, so nothing that reads the mod changes.
    python gen/nrm_reproducible.py <file.nrm>
"""
import io, sys, zipfile, zlib

FIXED = (1980, 1, 1, 0, 0, 0)


def rewrite(path):
    with zipfile.ZipFile(path) as z:
        members = [(i.filename, z.read(i)) for i in z.infolist() if not i.is_dir()]
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w') as z:
        for name, data in members:
            info = zipfile.ZipInfo(name, date_time=FIXED)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 0          # (MS-DOS: the same "made by" whatever the PC)
            info.external_attr = 0
            z.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=6)
    blob = out.getvalue()
    with zipfile.ZipFile(io.BytesIO(blob)) as z:      # (the same members, byte for byte, or nothing is written)
        if [(i.filename, z.read(i)) for i in z.infolist()] != members:
            raise SystemExit('nrm_reproducible: the rewritten zip does not hold the same members')
    open(path, 'wb').write(blob)
    print('%s: %d members, fixed dates (zlib %s)' % (path, len(members), zlib.ZLIB_VERSION))


if __name__ == '__main__':
    for p in sys.argv[1:]:
        rewrite(p)
