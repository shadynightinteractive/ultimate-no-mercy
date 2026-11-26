// [commentary2 2026-11-18] See wwise_decode.h. Follows vgmstream f7f7641: meta/wwise.c (parse_wwise + the VORBIS case of
// init_vgmstream_wwise_bnk), coding/vorbis_custom_decoder.c (init_vorbis_custom, read_packet, decode_frame,
// copy_samples) and base/sbuf.c (float -> s16: CONV_FLT_S16 without PCM16_ROUNDING_HALF = (int)(f * 32767.0f), clamped).
#include "wwise_decode.h"

#include <algorithm>
#include <cstring>

#include "vgmstream/coding/vorbis_custom_decoder.h"

namespace comm_build {

namespace {

uint32_t rd32(const uint8_t* p, bool be) { return be ? get_u32be(p) : get_u32le(p); }
uint16_t rd16(const uint8_t* p, bool be) { return be ? get_u16be(p) : get_u16le(p); }

}  // namespace

bool wem_decode(const uint8_t* data, size_t size, std::vector<int16_t>& out, int& rate, std::string& why) {
    out.clear();
    if (size < 0x0C || (std::memcmp(data, "RIFF", 4) != 0 && std::memcmp(data, "RIFX", 4) != 0)) { why = "not a Wwise file"; return false; }
    const bool be = std::memcmp(data, "RIFX", 4) == 0;
    if (std::memcmp(data + 8, "WAVE", 4) != 0) { why = "not a Wwise WAVE file"; return false; }

    // ---- parse_wwise: the chunks (even-aligned, no padding byte added), then the pseudo-WAVEFORMATEX
    uint32_t fmt_off = 0, fmt_size = 0, data_off = 0, data_size = 0, vorb_off = 0, vorb_size = 0, smpl_off = 0, smpl_size = 0, meta_off = 0;
    for (uint32_t cur = 0x0C; cur < size && size - cur >= 8;) {
        const uint32_t type = get_u32be(data + cur), csize = rd32(data + cur + 4, be);
        if (type == 0xFFFFFFFF || csize == 0xFFFFFFFF) break;
        const uint32_t off = cur + 8;
        cur = off + csize;
        if (type == 0) break;
        switch (type) {
            case 0x666d7420: fmt_off = off; fmt_size = csize; break;     // "fmt "
            case 0x64617461: data_off = off; data_size = csize; break;   // "data"
            case 0x766F7262: vorb_off = off; vorb_size = csize; break;   // "vorb"
            case 0x736D706C: smpl_off = off; smpl_size = csize; break;   // "smpl"
            case 0x6D657461: meta_off = off; break;                      // "meta"
            case 0x66616374: why = "unexpected fact chunk"; return false;
            default:
                if ((uint64_t)off + csize > size) { why = "broken .wem"; return false; }
        }
    }
    if (fmt_size < 0x10 || !data_off || (uint64_t)fmt_off + fmt_size > size) { why = "no fmt/data chunk"; return false; }
    const uint16_t format = rd16(data + fmt_off, be), channels = rd16(data + fmt_off + 2, be);
    rate = (int)rd32(data + fmt_off + 4, be);
    const uint16_t block_size = rd16(data + fmt_off + 0x0C, be), bits = rd16(data + fmt_off + 0x0E, be);
    const uint16_t extra_size = fmt_size > 0x10 ? rd16(data + fmt_off + 0x10, be) : 0;
    if (format != 0xFFFF) { why = "not Vorbis"; return false; }
    if (channels != 1) { why = "not mono"; return false; }
    if (block_size != 0 || bits != 0) { why = "bad Vorbis fmt"; return false; }
    if (smpl_off && smpl_size >= 0x34 && rd32(data + smpl_off + 0x1C, be) == 1 && rd32(data + smpl_off + 0x28, be) == 0) {
        why = "a looping file";   // (vgmstream-cli would play it twice with a fade: not a commentary line)
        return false;
    }
    if ((uint64_t)data_off + data_size > size) { why = "truncated .wem (prefetch)"; return false; }

    // ---- the VORBIS case
    vorbis_custom_config cfg{};
    cfg.channels = channels;
    cfg.sample_rate = rate;
    cfg.big_endian = be;
    cfg.stream_end = data_off + data_size;
    uint32_t extra_off, xsize, data_offsets, block_offsets;
    if (vorb_off) { extra_off = vorb_off; xsize = vorb_size; }
    else { extra_off = fmt_off + 0x18; xsize = extra_size; }
    switch (xsize) {
        case 0x2C: case 0x28: data_offsets = 0x18; block_offsets = 0; cfg.ww_version = WWVORBIS_V34; break;
        case 0x34: data_offsets = 0x18; block_offsets = 0x30; cfg.ww_version = WWVORBIS_V48; break;
        case 0x32: data_offsets = 0x18; block_offsets = 0x30; cfg.ww_version = WWVORBIS_V52; break;
        case 0x2a: data_offsets = 0x10; block_offsets = 0x28; cfg.ww_version = WWVORBIS_V53; break;
        case 0x30: data_offsets = 0x10; block_offsets = 0x28; cfg.ww_version = WWVORBIS_V62; break;   // (.wem: v62 first)
        default: why = "unknown Wwise Vorbis version"; return false;
    }
    if ((uint64_t)extra_off + std::max(data_offsets + 8, block_offsets + 2) > size) { why = "short Vorbis header"; return false; }
    const int32_t num_samples = (int32_t)rd32(data + extra_off, be);
    const uint32_t seek_size = rd32(data + extra_off + data_offsets, be);
    const uint32_t audio_offset = rd32(data + extra_off + data_offsets + 4, be);
    if (block_offsets) {
        cfg.blocksize_1_exp = data[extra_off + block_offsets + 0];
        cfg.blocksize_0_exp = data[extra_off + block_offsets + 1];
    }
    const uint32_t start_offset = data_off;
    if (cfg.ww_version == WWVORBIS_V48) {
        if ((uint64_t)start_offset + seek_size + 10 > size) { why = "short setup"; return false; }
        const uint32_t setup_size = rd16(data + start_offset + seek_size, be);
        const uint32_t setup_id = get_u32be(data + start_offset + seek_size + 6);
        if ((setup_id & 0x00FFFFFF) == 0x00424356) cfg.ww_version = WWVORBIS_V38;   // "\0BCV": inline codebooks
        else if (setup_size > 0x200) cfg.ww_version = WWVORBIS_V44;
    }
    uint32_t setup_offset = start_offset + seek_size;
    if (cfg.ww_version >= WWVORBIS_V56 && meta_off) setup_offset += rd32(data + meta_off + 4, be);
    if (num_samples <= 0) { why = "no samples"; return false; }

    STREAMFILE sf{data, size};
    // init_vorbis_custom (tried as v62, then v56, as wwise.c does)
    vorbis_custom_codec_data* d = nullptr;
    auto init = [&](wwise_version_t v) -> vorbis_custom_codec_data* {
        cfg.ww_version = v;
        vorbis_custom_codec_data* c = new vorbis_custom_codec_data();
        c->buffer_size = 0x8000;
        c->buffer = new uint8_t[c->buffer_size]();
        c->type = VORBIS_WWISE;
        c->config = cfg;
        vorbis_info_init(&c->vi);
        vorbis_comment_init(&c->vc);
        c->op.packet = c->buffer;
        c->op.b_o_s = 1;
        bool ok = vorbis_custom_setup_init_wwise(&sf, setup_offset, c) != 0;
        c->op.b_o_s = 0;
        if (ok) ok = vorbis_synthesis_init(&c->vd, &c->vi) == 0;
        if (ok) ok = vorbis_block_init(&c->vd, &c->vb) == 0;
        if (!ok) {
            // (free_vorbis_custom; clearing a never-initialised dsp/block is safe in libvorbis: zeroed structs)
            vorbis_block_clear(&c->vb);
            vorbis_dsp_clear(&c->vd);
            vorbis_comment_clear(&c->vc);
            vorbis_info_clear(&c->vi);
            delete[] c->buffer;
            delete c;
            return nullptr;
        }
        return c;
    };
    d = init(cfg.ww_version);
    if (!d && cfg.ww_version >= WWVORBIS_V56) d = init(cfg.ww_version == WWVORBIS_V62 ? WWVORBIS_V56 : WWVORBIS_V62);
    if (!d) { why = "Vorbis setup rejected"; return false; }

    // ---- decode: num_samples samples; what the decoder can't give stays silent (vgmstream's render on a decode fail)
    out.assign((size_t)num_samples, 0);
    size_t have = 0;
    VGMSTREAMCHANNEL ch{&sf, (off_t)(start_offset + audio_offset)};
    bool failed = false;
    while (have < out.size() && !failed) {
        float** pcm = nullptr;
        int got = vorbis_synthesis_pcmout(&d->vd, &pcm);
        if (got > 1024) got = 1024;                      // (VORBIS_CALL_SAMPLES)
        if (got > 0) {
            const size_t take = std::min<size_t>((size_t)got, out.size() - have);
            for (size_t i = 0; i < take; i++) {
                int v = (int)(pcm[0][i] * 32767.0f);
                out[have + i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            }
            have += take;
            vorbis_synthesis_read(&d->vd, got);
            continue;
        }
        if ((uint32_t)ch.offset >= d->config.stream_end) break;                 // read_packet: stream end
        if (!vorbis_custom_parse_packet_wwise(&ch, d)) { failed = true; break; }
        const int rc = vorbis_synthesis(&d->vb, &d->op);
        if (rc == OV_ENOTAUDIO) continue;
        if (rc != 0 || vorbis_synthesis_blockin(&d->vd, &d->vb) != 0) { failed = true; break; }
        d->op.packetno++;
    }
    vorbis_block_clear(&d->vb);
    vorbis_dsp_clear(&d->vd);
    vorbis_comment_clear(&d->vc);
    vorbis_info_clear(&d->vi);
    delete[] d->buffer;
    delete d;
    if (failed && have == 0) { why = "Vorbis decode error"; return false; }
    return true;
}

}  // namespace comm_build
