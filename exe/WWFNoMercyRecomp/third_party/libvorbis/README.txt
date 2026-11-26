libvorbis 1.3.7 (Xiph.Org, BSD-style licence: COPYING), from the official release libvorbis-1.3.7.tar.gz
(sha256 0e982409a9c3fc82ee06e08205b1355e5c6aa4c36bca58146ef399621b0ce5ab): the library's lib/ sources (less
vorbisfile.c and the tuning tools) and include/vorbis/codec.h + vorbisenc.h, unchanged except one marked spot in
lib/os.h (C2_GLIBC_ROUNDING: keep C's rint on Windows; see third_party/vorbis_compat). Used by the commentary builder
(src/main/commentary/packbuild) to decode WWE '13's Wwise Vorbis and to encode the pack's clips the way FFmpeg's
libvorbis encoder does.
