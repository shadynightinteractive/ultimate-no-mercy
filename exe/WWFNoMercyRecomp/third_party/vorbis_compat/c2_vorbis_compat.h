/* [commentary2 2026-11-18] Forced into each libvorbis file (-include c2_vorbis_compat.h) so that libvorbis gives the same
 * bytes on every platform as on Linux/glibc, where the developer's commentary pack was encoded and its WWE '13 lines
 * decoded. Three things differ between platforms and change libvorbis's output:
 *  - rint: libvorbis's os.h replaces it on Windows by floor(x + 0.5) (halves up, not to even) -> C2_GLIBC_ROUNDING
 *    keeps the real rint (the one line changed in libvorbis, marked there).
 *  - qsort: libvorbis sorts with ties (psy.c apsort: equal magnitudes); glibc's qsort is a stable merge sort, Windows'
 *    is not -> c2_qsort_stable (a stable merge sort gives exactly glibc's order).
 *  - exp/log/pow/atan/sin/cos: platform libms round differently; glibc's are correctly rounded in practically every
 *    case -> CORE-MATH's correctly rounded versions (third_party/coremath).
 * Measured (tools/commentary/test_platform.py, 150 clips): the rint fix alone takes Windows from 124/150 to 150/150
 * equal to the Linux/glibc build; qsort and CORE-MATH changed none of those 150 but remove the remaining platform
 * dependence (Windows' qsort order on ties, UCRT's rounding) for inputs not tested; they cost ~13% encode time. */
#ifndef C2_VORBIS_COMPAT_H
#define C2_VORBIS_COMPAT_H
#define C2_GLIBC_ROUNDING 1
#include <stddef.h>
#include <stdlib.h>
#include <math.h>
#ifdef __cplusplus
extern "C" {
#endif
double cr_exp(double x);
double cr_log(double x);
double cr_pow(double x, double y);
double cr_atan(double x);
double cr_sin(double x);
double cr_cos(double x);
void c2_qsort_stable(void* base, size_t n, size_t size, int (*cmp)(const void*, const void*));
#ifdef __cplusplus
}
#endif
#define exp(x) cr_exp(x)
#define log(x) cr_log(x)
#define pow(x, y) cr_pow(x, y)
#define atan(x) cr_atan(x)
#define sin(x) cr_sin(x)
#define cos(x) cr_cos(x)
#define qsort(b, n, s, c) c2_qsort_stable(b, n, s, c)
#endif
