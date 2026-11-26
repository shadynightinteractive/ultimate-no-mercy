/* [commentary2 2026-11-18] A stable merge sort with qsort's interface, for libvorbis (see c2_vorbis_compat.h): with a
 * consistent comparator a stable sort's result is unique, so it is the order glibc's qsort (msort.c, stable) gives. */
#include <stdlib.h>
#include <string.h>

static void merge_pass(char* a, char* tmp, size_t n, size_t size, int (*cmp)(const void*, const void*)) {
    if (n < 2) return;
    const size_t half = n / 2;
    merge_pass(a, tmp, half, size, cmp);
    merge_pass(a + half * size, tmp, n - half, size, cmp);
    size_t i = 0, j = half, k = 0;
    while (i < half && j < n) {
        /* take from the right only when strictly smaller: equal elements keep their order */
        if (cmp(a + j * size, a + i * size) < 0) memcpy(tmp + (k++) * size, a + (j++) * size, size);
        else memcpy(tmp + (k++) * size, a + (i++) * size, size);
    }
    while (i < half) memcpy(tmp + (k++) * size, a + (i++) * size, size);
    while (j < n) memcpy(tmp + (k++) * size, a + (j++) * size, size);
    memcpy(a, tmp, n * size);
}

void c2_qsort_stable(void* base, size_t n, size_t size, int (*cmp)(const void*, const void*)) {
    if (n < 2 || size == 0) return;
    char* tmp = (char*)malloc(n * size);
    if (!tmp) { qsort(base, n, size, cmp); return; }   /* (out of memory: glibc falls back the same way) */
    merge_pass((char*)base, tmp, n, size, cmp);
    free(tmp);
}
