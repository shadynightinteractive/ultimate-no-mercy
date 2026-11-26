/* [commentary2 2026-11-18] forced into each CORE-MATH file (-include c2_fma.h, with -D__builtin_fma=c2_fma): its fused
 * multiply-adds go to musl's exact software fma (c2_fma.c), the same result as an FMA instruction, on any CPU. */
#ifndef C2_FMA_H
#define C2_FMA_H
double c2_fma(double x, double y, double z);
/* C23's roundeven (round half to even), which not every C library has: rint in the default rounding mode */
double c2_roundeven(double x);
#define roundeven(x) c2_roundeven(x)
#endif
