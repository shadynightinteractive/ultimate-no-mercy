CORE-MATH (https://core-math.gitlabpages.inria.fr, MIT licence: LICENSE): correctly rounded binary64 exp, log, pow, atan,
sin, cos, fetched 2026-11-18 from gitlab.inria.fr/core-math/core-math (master, src/binary64/<f>/), unchanged.
c2_fma.c: musl 1.2.5's software fma (MIT: musl_COPYRIGHT), renamed c2_fma, plus c2_roundeven. c2_fma.h: ours.
Built with -D__builtin_fma=c2_fma -D__builtin_roundeven=c2_roundeven -include c2_fma.h. Used only by the commentary
builder's libvorbis, through third_party/vorbis_compat/c2_vorbis_compat.h (which says why).
