/* [commentary2 2026-11-18] build_header_comment / build_header_identification, copied unchanged from vgmstream f7f7641
 * coding/vorbis_custom_utils.c (ISC licence: ../COPYING); the rest of that file is not needed here. */
#include "vorbis_custom_decoder.h"

int build_header_comment(uint8_t* buf, int bufsize) {
    int bytes = 0x19;

    if (bytes > bufsize) return 0;

    put_u8   (buf+0x00, 0x03);              /* packet_type (comments) */
    memcpy   (buf+0x01, "vorbis", 6);       /* id */
    put_u32le(buf+0x07, 0x09);              /* vendor_length */
    memcpy   (buf+0x0b, "vgmstream", 9);    /* vendor_string */
    put_u32le(buf+0x14, 0x00);              /* user_comment_list_length */
    put_u8   (buf+0x18, 0x01);              /* framing_flag (fixed) */

    return bytes;
}

int build_header_identification(uint8_t* buf, int bufsize, vorbis_custom_config* cfg) {
    int bytes = 0x1e;

    if (bytes > bufsize)
        return 0;

    // long (bigger frames) + short (smaller frames) blocksizes (samples per frame)
    uint8_t blocksizes = (cfg->blocksize_0_exp << 4) | (cfg->blocksize_1_exp);

    put_u8   (buf+0x00, 0x01);              /* packet_type (id) */
    memcpy   (buf+0x01, "vorbis", 6);       /* id */
    put_u32le(buf+0x07, 0x00);              /* vorbis_version (fixed) */
    put_u8   (buf+0x0b, cfg->channels);     /* audio_channels */
    put_u32le(buf+0x0c, cfg->sample_rate);  /* audio_sample_rate */
    put_u32le(buf+0x10, 0x00);              /* bitrate_maximum (optional hint) */
    put_u32le(buf+0x14, 0x00);              /* bitrate_nominal (optional hint) */
    put_u32le(buf+0x18, 0x00);              /* bitrate_minimum (optional hint) */
    put_u8   (buf+0x1c, blocksizes);        /* blocksize_0 + blocksize_1 nibbles */
    put_u8   (buf+0x1d, 0x01);              /* framing_flag (fixed) */

    return bytes;
}
