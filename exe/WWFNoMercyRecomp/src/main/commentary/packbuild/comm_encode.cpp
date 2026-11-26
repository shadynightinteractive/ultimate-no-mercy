// [commentary2 2026-11-18] See comm_encode.h. Each step names the FFmpeg code it reproduces (FFmpeg 6.1.1 = the
// version that made the pack: its clips' comment says Lavc60.31.102; the parts used here are unchanged up to 8.1).
// Build this file (and libvorbis) WITHOUT floating-point contraction (-ffp-contract=off): FFmpeg's and libvorbis's
// float arithmetic is plain multiply-then-add, and a fused multiply-add would round differently.
#include "comm_encode.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>

#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>

namespace comm_build {

// ---- silence trim: libavfilter/af_silenceremove.c + silenceremove_template.c (detection rms, window 0.02 s,
//      start_periods 1, start_duration 0, start_silence 0, start_mode any), in float as FFmpeg runs it for s16 input
//      (format negotiation picks AV_SAMPLE_FMT_FLT: the nearest to s16).
namespace {

// the index of the first sample the filter lets through, scanning x[0..n) forwards (step +1) or backwards (step -1);
// n if none
size_t first_loud(const float* x, size_t n, ptrdiff_t step, int rate) {
    // window_duration = av_rescale(20000 us, rate, 1000000), at least 1 (av_rescale rounds to nearest)
    const int64_t wd = std::max<int64_t>(1, (20000LL * rate + 500000) / 1000000);
    // start_threshold "-50dB": libavutil/opt.c turns "dB" into ff_exp10(v / 20) = exp2(M_LOG2_10 * v / 20) (a double),
    // which the filter reads as a float
    const float threshold = (float)std::exp2(3.32192809488736234787 * (-50.0 / 20.0));
    std::vector<float> window((size_t)wd, 0.0f);
    int window_pos = 0, window_size = 0;
    float cache = 0.0f;
    for (size_t k = 0; k < n; k++) {
        const float sample = step > 0 ? x[k] : x[n - 1 - k];
        const int wpos = window_pos;                    // (start_wpos: taken before queue_sample moves it)
        if (window_size < wd) window_size++;            // queue_sample
        if (++window_pos >= wd) window_pos = 0;
        const float old = window[(size_t)wpos];
        // compute_rms_flt
        cache += sample * sample;
        cache -= old * old;
        cache = std::fmax(cache, 0.0f);
        const float r = std::sqrt(cache / (float)window_size);
        window[(size_t)wpos] = sample;
        if (r > threshold) return k;                    // start_sample_count 1 > start_duration 0: found; this sample on
    }
    return n;
}

}  // namespace

void trim_silence(const std::vector<float>& x, int rate, size_t& first, size_t& last) {
    first = first_loud(x.data(), x.size(), +1, rate);
    if (first == x.size()) { last = first; return; }
    // areverse, the same filter again, areverse: the end trimmed the same way, on what the first pass kept
    const size_t kept = x.size() - first;
    const size_t from_end = first_loud(x.data() + first, kept, -1, rate);
    last = x.size() - from_end;
}

// ---- Ogg ----------------------------------------------------------------------------------------------------------
uint32_t ogg_crc(const uint8_t* p, size_t n, uint32_t crc) {
    static uint32_t table[256];
    static bool made = false;
    if (!made) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t r = i << 24;
            for (int k = 0; k < 8; k++) r = (r & 0x80000000u) ? (r << 1) ^ 0x04C11DB7u : (r << 1);
            table[i] = r;
        }
        made = true;
    }
    for (size_t i = 0; i < n; i++) crc = (crc << 8) ^ table[((crc >> 24) ^ p[i]) & 0xFF];
    return crc;
}

namespace {

struct Page {
    uint8_t flags = 0;
    int64_t granule = -1;
    std::vector<uint8_t> lacing;
    std::vector<uint8_t> body;
};

void put_page(std::vector<uint8_t>& out, const Page& pg, uint32_t serial, uint32_t seq) {
    const size_t at = out.size();
    const uint8_t head[4] = {'O', 'g', 'g', 'S'};
    out.insert(out.end(), head, head + 4);
    out.push_back(0);
    out.push_back(pg.flags);
    for (int i = 0; i < 8; i++) out.push_back((uint8_t)((uint64_t)pg.granule >> (8 * i)));
    for (int i = 0; i < 4; i++) out.push_back((uint8_t)(serial >> (8 * i)));
    for (int i = 0; i < 4; i++) out.push_back((uint8_t)(seq >> (8 * i)));
    for (int i = 0; i < 4; i++) out.push_back(0);                // CRC, filled below
    out.push_back((uint8_t)pg.lacing.size());
    out.insert(out.end(), pg.lacing.begin(), pg.lacing.end());
    out.insert(out.end(), pg.body.begin(), pg.body.end());
    const uint32_t crc = ogg_crc(out.data() + at, out.size() - at);
    for (int i = 0; i < 4; i++) out[at + 22 + i] = (uint8_t)(crc >> (8 * i));
}

std::vector<uint8_t> lacing_of(size_t n) {
    std::vector<uint8_t> l(n / 255, 255);
    l.push_back((uint8_t)(n % 255));
    return l;
}

// av_rescale_q(a, 1/rate, 1/1000000): to nearest, halves away from zero
int64_t to_us(int64_t a, int rate) {
    const int64_t num = a * 1000000;
    return num >= 0 ? (num + rate / 2) / rate : -((-num + rate / 2) / rate);
}

}  // namespace

// ---- the encoder -------------------------------------------------------------------------------------------------
std::vector<uint8_t> encode_clip(const float* x, size_t n, int rate, const std::vector<uint8_t>& comment, uint32_t serial) {
    std::vector<uint8_t> out;
    vorbis_info vi;
    vorbis_info_init(&vi);
    // libavcodec/libvorbisenc.c libvorbis_setup: "-q:a 0" -> vorbis_encode_setup_vbr(vi, channels, rate, 0 / 10.0)
    if (vorbis_encode_setup_vbr(&vi, 1, rate, (float)(0 / 10.0)) != 0 || vorbis_encode_setup_init(&vi) != 0) {
        vorbis_info_clear(&vi);
        return out;
    }
    vorbis_dsp_state vd;
    vorbis_block vb;
    vorbis_comment vc;
    vorbis_analysis_init(&vd, &vi);
    vorbis_block_init(&vd, &vb);
    vorbis_comment_init(&vc);
    ogg_packet ident, comm, setup;
    vorbis_analysis_headerout(&vd, &vc, &ident, &comm, &setup);   // (our comment packet replaces comm below)
    const std::vector<uint8_t> ident_b(ident.packet, ident.packet + ident.bytes);
    const std::vector<uint8_t> setup_b(setup.packet, setup.packet + setup.bytes);

    // libvorbis_get_priming_samples: a second analysis fed nothing gives the priming packet; FFmpeg's Vorbis parser
    // (libavcodec/vorbis_parser.c, previous_blocksize starting at mode 0's blocksize) gives its duration =
    // avctx->initial_padding, and leaves previous_blocksize at that packet's blocksize
    long prev_bs = 0, padding = 0;
    {
        vorbis_dsp_state pd;
        vorbis_block pb;
        vorbis_analysis_init(&pd, &vi);
        vorbis_block_init(&pd, &pb);
        vorbis_analysis_wrote(&pd, 0);
        ogg_packet op{};
        long bs = 0;
        if (vorbis_analysis_blockout(&pd, &pb) == 1) {
            vorbis_analysis(&pb, nullptr);
            vorbis_bitrate_addblock(&pb);
            if (vorbis_bitrate_flushpacket(&pd, &op) == 1) bs = vorbis_packet_blocksize(&vi, &op);
        }
        // mode 0's blocksize: a packet whose mode bits are 0 (byte 0 = audio flag 0 + mode 0)
        uint8_t zero = 0;
        ogg_packet m0{};
        m0.packet = &zero;
        m0.bytes = 1;
        prev_bs = vorbis_packet_blocksize(&vi, &m0);
        padding = bs > 0 ? (prev_bs + bs) >> 2 : 0;
        if (bs > 0) prev_bs = bs;
        vorbis_block_clear(&pb);
        vorbis_dsp_clear(&pd);
    }

    // libvorbis_encode_frame: 64-sample frames (LIBVORBIS_FRAME_SIZE), the last one shorter; EOF = wrote(0)
    std::vector<std::vector<uint8_t>> packets;
    auto drain = [&]() {
        while (vorbis_analysis_blockout(&vd, &vb) == 1) {
            if (vorbis_analysis(&vb, nullptr) < 0) break;
            if (vorbis_bitrate_addblock(&vb) < 0) break;
            ogg_packet op;
            while (vorbis_bitrate_flushpacket(&vd, &op) == 1) packets.emplace_back(op.packet, op.packet + op.bytes);
        }
    };
    for (size_t at = 0; at < n; at += 64) {
        const int k = (int)std::min<size_t>(64, n - at);
        float** buf = vorbis_analysis_buffer(&vd, k);
        std::memcpy(buf[0], x + at, sizeof(float) * (size_t)k);
        vorbis_analysis_wrote(&vd, k);
        drain();
    }
    if (n > 0) vorbis_analysis_wrote(&vd, 0);   // ("if (!s->eof && s->afq.frame_alloc)": only after a frame was given)
    drain();

    // ---- header pages: oggpk.replace_comment -- the identification packet on a page of its own (BOS), then the
    //      comment and setup packets laced onto as few pages as 255 lacing values allow
    uint32_t seq = 0;
    {
        Page p;
        p.flags = 2;
        p.granule = 0;
        p.lacing = lacing_of(ident_b.size());
        p.body = ident_b;
        put_page(out, p, serial, seq++);
        std::vector<uint8_t> lace = lacing_of(comment.size()), l2 = lacing_of(setup_b.size());
        lace.insert(lace.end(), l2.begin(), l2.end());
        std::vector<uint8_t> hdr = comment;
        hdr.insert(hdr.end(), setup_b.begin(), setup_b.end());
        size_t li = 0, hi = 0;
        bool prev_ended_255 = false;
        while (li < lace.size()) {
            Page q;
            const size_t take = std::min<size_t>(255, lace.size() - li);
            q.lacing.assign(lace.begin() + (ptrdiff_t)li, lace.begin() + (ptrdiff_t)(li + take));
            size_t bytes = 0;
            for (uint8_t v : q.lacing) bytes += v;
            q.body.assign(hdr.begin() + (ptrdiff_t)hi, hdr.begin() + (ptrdiff_t)(hi + bytes));
            q.flags = prev_ended_255 ? 1 : 0;
            q.granule = 0;
            prev_ended_255 = q.lacing.back() == 255;
            li += take;
            hi += bytes;
            put_page(out, q, serial, seq++);
        }
    }

    // ---- audio pages: libavformat/oggenc.c ogg_buffer_data / ogg_write_packet_internal, with each packet's pts and
    //      duration from libvorbisenc.c (vorbis parser duration, then ff_af_queue_remove) and page_duration 1 s
    {
        int64_t remaining = (int64_t)n + padding;   // the audio frame queue: the samples + the initial padding
        int64_t done = 0;
        const int64_t pts0 = -padding;
        int64_t start = pts0;                       // page.start_granule
        Page page;
        std::vector<Page> pages;
        for (const std::vector<uint8_t>& p : packets) {
            ogg_packet op{};
            op.packet = const_cast<unsigned char*>(p.data());
            op.bytes = (long)p.size();
            const long cur = vorbis_packet_blocksize(&vi, &op);
            const int64_t dur = cur > 0 ? (prev_bs + cur) >> 2 : 0;
            if (cur > 0) prev_bs = cur;
            const int64_t removed = std::min(dur, remaining);
            remaining -= removed;
            const int64_t granule = pts0 + done + removed;
            done += removed;
            // "avoid a continued page"
            if (!page.lacing.empty() && 65025 - page.body.size() < p.size()) {
                start = page.granule;
                pages.push_back(std::move(page));
                page = Page();
            }
            const size_t total_segments = p.size() / 255 + 1;
            size_t i = 0, off = 0;
            while (i < total_segments) {
                const size_t segs = std::min(total_segments - i, 255 - page.lacing.size());
                if (i && page.lacing.empty()) page.flags |= 1;
                page.lacing.insert(page.lacing.end(), segs - 1, 255);
                const size_t len = std::min(p.size() - off, segs * 255);
                page.lacing.push_back((uint8_t)(len - (segs - 1) * 255));
                page.body.insert(page.body.end(), p.begin() + (ptrdiff_t)off, p.begin() + (ptrdiff_t)(off + len));
                off += len;
                i += segs;
                if (i == total_segments) page.granule = granule;
                if (page.lacing.size() == 255 || to_us(page.granule, rate) - to_us(start, rate) >= 1000000) {
                    start = page.granule;
                    pages.push_back(std::move(page));
                    page = Page();
                }
            }
        }
        if (!page.lacing.empty()) pages.push_back(std::move(page));
        if (!pages.empty()) pages.back().flags |= 4;   // ogg_write_trailer: the last page is EOS
        for (const Page& p : pages) put_page(out, p, serial, seq++);
    }

    vorbis_comment_clear(&vc);
    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    vorbis_info_clear(&vi);
    return out;
}

// ---- decode (the judge's): plain libvorbis synthesis, the samples cut to the last page's granule ----------------------
bool decode_clip(const uint8_t* p, size_t n, std::vector<float>& out) {
    out.clear();
    ogg_sync_state oy; ogg_stream_state os; ogg_page og; ogg_packet op;
    vorbis_info vi; vorbis_comment vc; vorbis_dsp_state vd; vorbis_block vb;
    ogg_sync_init(&oy);
    vorbis_info_init(&vi);
    vorbis_comment_init(&vc);
    char* buf = ogg_sync_buffer(&oy, (long)n);
    std::memcpy(buf, p, n);
    ogg_sync_wrote(&oy, (long)n);
    int headers = 0;
    bool started = false, ok = true, synth = false;
    int64_t last_granule = -1;
    while (ok && ogg_sync_pageout(&oy, &og) == 1) {
        if (!started) { ogg_stream_init(&os, ogg_page_serialno(&og)); started = true; }
        if (ogg_stream_pagein(&os, &og) != 0) { ok = false; break; }
        if (ogg_page_granulepos(&og) >= 0) last_granule = ogg_page_granulepos(&og);
        while (ogg_stream_packetout(&os, &op) == 1) {
            if (headers < 3) {
                if (vorbis_synthesis_headerin(&vi, &vc, &op) != 0) { ok = false; break; }
                if (++headers == 3) {
                    if (vi.channels != 1) { ok = false; break; }
                    vorbis_synthesis_init(&vd, &vi);
                    vorbis_block_init(&vd, &vb);
                    synth = true;
                }
                continue;
            }
            if (vorbis_synthesis(&vb, &op) == 0) vorbis_synthesis_blockin(&vd, &vb);
            float** pcm;
            int got;
            while ((got = vorbis_synthesis_pcmout(&vd, &pcm)) > 0) {
                out.insert(out.end(), pcm[0], pcm[0] + got);
                vorbis_synthesis_read(&vd, got);
            }
        }
    }
    if (synth) { vorbis_block_clear(&vb); vorbis_dsp_clear(&vd); }
    if (started) ogg_stream_clear(&os);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);
    ogg_sync_clear(&oy);
    if (!ok || headers < 3 || last_granule < 0) { out.clear(); return false; }
    out.resize((size_t)last_granule, 0.0f);
    return true;
}

}  // namespace comm_build
