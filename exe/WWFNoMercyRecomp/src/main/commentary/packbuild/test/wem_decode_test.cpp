// [commentary2 2026-11-18] Test CLI for wwise_decode:
//   wem_decode_test wem <in.wem> <out.raw>        the builder's decode (s16le mono)
//   wem_decode_test ogg <in.ogg> <out.raw>        the same Vorbis packets from a plain Ogg file through libvorbis, converted
//                                                 to 16 bits as vgmstream does ((int)(x * 32767.0f)), as many samples as
//                                                 its last granule -- what a .wem holding those packets must decode to
// tools/commentary/test_wem.py makes .wem files from Ogg files (ogg2wem.py) and compares the two.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <ogg/ogg.h>
#include <vorbis/codec.h>

#include "../wwise_decode.h"

static std::string slurp(const char* p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: wem_decode_test wem|ogg <in> <out.raw>\n"); return 2; }
    const std::string b = slurp(argv[2]);
    std::vector<int16_t> out;
    if (std::strcmp(argv[1], "wem") == 0) {
        int rate = 0;
        std::string why;
        if (!comm_build::wem_decode((const uint8_t*)b.data(), b.size(), out, rate, why)) { std::fprintf(stderr, "%s\n", why.c_str()); return 3; }
    } else {
        ogg_sync_state oy; ogg_stream_state os; ogg_page og; ogg_packet op;
        vorbis_info vi; vorbis_comment vc; vorbis_dsp_state vd; vorbis_block vb;
        ogg_sync_init(&oy);
        vorbis_info_init(&vi); vorbis_comment_init(&vc);
        char* buf = ogg_sync_buffer(&oy, (long)b.size());
        std::memcpy(buf, b.data(), b.size());
        ogg_sync_wrote(&oy, (long)b.size());
        int headers = 0; bool started = false; int64_t last_granule = 0;
        std::vector<std::vector<uint8_t>> packets;
        while (ogg_sync_pageout(&oy, &og) == 1) {
            if (!started) { ogg_stream_init(&os, ogg_page_serialno(&og)); started = true; }
            ogg_stream_pagein(&os, &og);
            if (ogg_page_granulepos(&og) > 0) last_granule = ogg_page_granulepos(&og);
            while (ogg_stream_packetout(&os, &op) == 1) {
                if (headers < 3) { if (vorbis_synthesis_headerin(&vi, &vc, &op) != 0) return 4; if (++headers == 3) { vorbis_synthesis_init(&vd, &vi); vorbis_block_init(&vd, &vb); } continue; }
                if (vorbis_synthesis(&vb, &op) == 0) vorbis_synthesis_blockin(&vd, &vb);
                float** pcm; int n;
                while ((n = vorbis_synthesis_pcmout(&vd, &pcm)) > 0) {
                    for (int i = 0; i < n; i++) {
                        int v = (int)(pcm[0][i] * 32767.0f);
                        out.push_back((int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v));
                    }
                    vorbis_synthesis_read(&vd, n);
                }
            }
        }
        out.resize((size_t)last_granule, 0);
    }
    std::ofstream o(argv[3], std::ios::binary);
    o.write((const char*)out.data(), (std::streamsize)(out.size() * 2));
    std::printf("%zu samples\n", out.size());
    return 0;
}
