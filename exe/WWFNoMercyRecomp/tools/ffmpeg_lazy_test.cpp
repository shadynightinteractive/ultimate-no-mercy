// [xplat 2026-11-12] Checks the FFmpeg lazy loading (D3, src/main/arcade_ffmpeg_lazy.h) without the game: asks the media decoder for one
// video. With FFmpeg's libraries in lib/ beside this program it must decode pictures; without them it must say so and not crash.
//   ffmpeg_lazy_test <video file>        prints "[test] ..." lines; exit 0 when the result is what the folder's contents allow
#include <cstdio>
#include "arcade_media.h"
int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: ffmpeg_lazy_test <video>\n"); return 2; }
    std::string why;
    arcade_media::VideoOptions o; o.hardware = false;
    auto v = arcade_media::open_video(argv[1], o, why);
    if (!v) { printf("[test] open_video: refused (%s)\n", why.c_str()); return 3; }
    int n = 0; arcade_media::Picture p;
    while (v->next(p)) n++;
    printf("[test] open_video: ok, %dx%d, codec %s, decoder %s, %d pictures, sound %d\n", v->info().width, v->info().height, v->info().codec.c_str(), v->info().decoder.c_str(), n, v->info().has_sound);
    auto a = arcade_media::open_video_sound(argv[1], why);
    if (!a) { printf("[test] open_video_sound: refused (%s)\n", why.c_str()); return 4; }
    int16_t buf[4096 * 2]; size_t got = 0, k; while ((k = a->read(buf, 4096)) > 0) got += k;
    printf("[test] open_video_sound: ok, %d Hz, %zu frames, decoder %s\n", a->rate(), got, a->decoder());
    return 0;
}
