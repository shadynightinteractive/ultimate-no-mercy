/* [commentary2 2026-11-18] NOT vgmstream's file: a cut-down stand-in for vgmstream's coding/vorbis_custom_decoder.h +
 * the Wwise parts of coding/coding.h (vgmstream git f7f7641, 2026-10-19), with the same names and fields, so that
 * vorbis_custom_utils_wwise.c beside it compiles unchanged. Only Wwise Vorbis is kept. */
#ifndef C2_VORBIS_CUSTOM_DECODER_H
#define C2_VORBIS_CUSTOM_DECODER_H

#define VGM_USE_VORBIS 1
#include "../streamfile.h"
#include <vorbis/codec.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { VORBIS_WWISE = 1 } vorbis_custom_t;
typedef enum { WWVORBIS_V34, WWVORBIS_V38, WWVORBIS_V44, WWVORBIS_V48, WWVORBIS_V52, WWVORBIS_V53, WWVORBIS_V56, WWVORBIS_V62, } wwise_version_t;

typedef struct {
    /* to reconstruct init packets */
    int channels;
    int sample_rate;
    int blocksize_0_exp;
    int blocksize_1_exp;
    wwise_version_t ww_version;

    uint32_t setup_id; /* external setup */
    int big_endian; /* flag */
    uint32_t stream_end; /* optional, to avoid overreading into next subsong or chunk */

    /* output (kinda ugly here but to simplify) */
    off_t data_start_offset;
    int64_t last_granule;
} vorbis_custom_config;

typedef enum { WWV_HEADER_TRIAD, WWV_FULL_SETUP, WWV_INLINE_CODEBOOKS, WWV_EXTERNAL_CODEBOOKS, WWV_AOTUV603_CODEBOOKS } wwise_setup_t;
typedef enum { WWV_TYPE_8, WWV_TYPE_6, WWV_TYPE_2 } wwise_header_t;
typedef enum { WWV_STANDARD, WWV_MODIFIED } wwise_packet_t;

/* custom Vorbis without Ogg layer */
typedef struct vorbis_custom_codec_data {
    vorbis_info vi;             /* stream settings */
    vorbis_comment vc;          /* stream comments */
    vorbis_dsp_state vd;        /* decoder global state */
    vorbis_block vb;            /* decoder local state */
    ogg_packet op;              /* fake packet for internal use */

    uint8_t* buffer;            /* internal raw data buffer */
    size_t buffer_size;
    float* fbuf;

    int current_discard;        /* for looping purposes */

    vorbis_custom_t type;        /* Vorbis subtype */
    vorbis_custom_config config; /* config depending on the mode */

    /* Wwise Vorbis config */
    wwise_setup_t setup_type;
    wwise_header_t header_type;
    wwise_packet_t packet_type;

    /* Wwise Vorbis: saved data to reconstruct modified packets */
    uint8_t mode_blockflag[64+1];   /* max 6b+1; flags 'n stuff */
    int mode_bits;                  /* bits to store mode_number */
    uint8_t prev_blockflag;         /* blockflag in the last decoded packet */
} vorbis_custom_codec_data;

/* a channel's read position (vgmstream's VGMSTREAMCHANNEL, the two fields the Wwise packet reader uses) */
typedef struct {
    STREAMFILE* streamfile;
    off_t offset;
} VGMSTREAMCHANNEL;

int vorbis_custom_setup_init_wwise(STREAMFILE* sf, off_t start_offset, vorbis_custom_codec_data* data);
int vorbis_custom_parse_packet_wwise(VGMSTREAMCHANNEL* stream, vorbis_custom_codec_data* data);

/* other utils to make/parse vorbis stuff (vgmstream's coding/vorbis_custom_utils.c, in vorbis_custom_utils_min.c) */
int build_header_comment(uint8_t* buf, int bufsize);
int build_header_identification(uint8_t* buf, int bufsize, vorbis_custom_config* cfg);

#ifdef __cplusplus
}
#endif

#endif
