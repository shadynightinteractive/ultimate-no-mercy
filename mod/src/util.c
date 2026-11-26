// The compiler may emit calls to these for struct copies and clears.
#include "arcade.h"

void* memset(void* d, int c, unsigned int n) {
    u8* p = (u8*)d;
    while (n--) *p++ = (u8)c;
    return d;
}

void* memcpy(void* d, const void* s, unsigned int n) {
    u8* p = (u8*)d;
    const u8* q = (const u8*)s;
    while (n--) *p++ = *q++;
    return d;
}
