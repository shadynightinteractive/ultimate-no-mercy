zstd 1.5.6 decoder only (single-file amalgamation), BSD licence (see LICENSE).
From the official release tarball zstd-1.5.6.tar.gz (github.com/facebook/zstd releases), made with
`python -I build/single_file_libs/combine.py -r lib -x legacy/zstd_legacy.h -o zstddeclib.c build/single_file_libs/zstddeclib-in.c`;
zstd.h is lib/zstd.h from the same tarball. Build zstddeclib.c as C (it sets ZSTD_DISABLE_ASM itself).
Used by src/main/commentary/packbuild/wii_disc.cpp to read RVZ disc images.
