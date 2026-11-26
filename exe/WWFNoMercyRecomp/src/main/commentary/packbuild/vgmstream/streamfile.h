/* [commentary2 2026-11-18] NOT vgmstream's file: a small stand-in for the parts of vgmstream's streamfile.h / util
 * helpers that coding/vorbis_custom_utils_wwise.c (copied unchanged from vgmstream f7f7641) uses. A STREAMFILE here is
 * a .wem held in memory. Everything else in this folder is vgmstream's own code (ISC licence: COPYING). */
#ifndef C2_VGM_STREAMFILE_H
#define C2_VGM_STREAMFILE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>

typedef struct {
    const uint8_t* data;
    size_t size;
} STREAMFILE;

static inline size_t read_streamfile(uint8_t* dst, off_t offset, size_t length, STREAMFILE* sf) {
    if (offset < 0 || (size_t)offset >= sf->size) return 0;
    if (length > sf->size - (size_t)offset) length = sf->size - (size_t)offset;
    memcpy(dst, sf->data + offset, length);
    return length;
}
static inline size_t get_streamfile_size(STREAMFILE* sf) { return sf->size; }
static inline void close_streamfile(STREAMFILE* sf) { (void)sf; }

static inline uint16_t get_u16le(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint16_t get_u16be(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t get_u32le(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline uint32_t get_u32be(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3]; }
static inline int32_t get_s32le(const uint8_t* p) { return (int32_t)get_u32le(p); }
static inline int32_t get_s32be(const uint8_t* p) { return (int32_t)get_u32be(p); }
static inline void put_u8(uint8_t* p, uint8_t v) { p[0] = v; }
static inline void put_u32le(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

#define VGM_LOG(...) do { } while (0)

#endif
