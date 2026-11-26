# Credits and third-party work

This file names work by others that the project's own source follows or is built on, besides the vendored libraries
under `libs/`, `exe/WWFNoMercyRecomp/lib/` and the `third_party/` folders (each keeps its own licence file beside it).

## N64 VADPCM encoder and decoder (the ROM importer)

**What:** `exe/WWFNoMercyRecomp/src/romimport/ri_vadpcm.cpp` and `ri_vadpcm.h` -- a C++ port of the Nintendo 64 SDK's
audio tools `tabledesign` (the ADPCM codebook design: `tabledesign.c`, `estimate.c`, `codebook.c`, `print.c`) and
`vadpcm_enc` / `vadpcm_dec` (the encoder and decoder: `vpredictor.c`, `vencode.c`, `quant.c`, `vdecode.c`,
`sampleio.c`). The port follows those tools' algorithms and their arithmetic step for step -- except one part that is our
own (below) -- so a theme the first start encodes is byte for byte what the tools make (checked by `tools/romimport/native/vadpcm_check.py`). The developer
build's Python side (`mod/gen/vadpcm.py`) runs the same tools themselves; it does not contain them.

**From:** the tools as reconstructed by the Super Mario 64 decompilation project, n64decomp/sm64,
<https://github.com/n64decomp/sm64>, folder `tools/sdk-tools` (`adpcm/` and `tabledesign/`).

**Licence:** that repository is released under **CC0 1.0 Universal** (its `LICENSE.md`; no other notice in
`tools/sdk-tools`). CC0 waives its authors' rights; it asks for no attribution, and we give it here anyway.

**Notes:**
- The SDK tools themselves were Nintendo's / Silicon Graphics' programs; the sm64 project's files are its
  reconstruction of them. CC0 covers what the sm64 contributors could waive.
- **Ours, not ported:** the per-frame linear solve, `lu_decompose` and `lu_solve` in `ri_vadpcm.cpp`. In the sm64 source
  the tool's two routines for it, `lud` and `lubksb` (in `estimate.c`), are marked as derived from `ludcmp` / `lubksb` in
  *Numerical Recipes in C: The Art of Scientific Computing* (Press, Teukolsky, Vetterling, Flannery), whose licence
  restricts redistributing its code. So the port does not follow them: the two functions were written from scratch from
  the mathematics (an LU factorisation with partial pivoting and row scaling, then forward and back substitution), not from
  that code, keeping only the order of the floating-point operations the results depend on. Every theme the importer
  encodes is still the same bytes (relclean, 2026-11-19, the user's decision of that day).

(Recorded 2026-11-19, relcheck: publishing the VADPCM port was approved by the user on 2026-11-19 -- "I looked at your
list you needed from me and I concur with your recommendations" -- with this credit.)
