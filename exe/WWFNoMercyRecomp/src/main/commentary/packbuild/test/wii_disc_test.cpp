// [arcade] Test tool for wii_disc (the disc image reader):
//   wii_disc_test <image> list                 game id, then "size<TAB>offset<TAB>path" per file
//   wii_disc_test <image> extract <path> <out> one file to <out>
//   wii_disc_test <image> extractall <dir>     every file under <dir> (its disc path)
// Exit code 0 on success, 1 with the reader's reason on stderr otherwise.
#include "../wii_disc.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <vector>

namespace {

// Copies one file out in 1 MiB steps (so big files are read piecewise, as the builder will).
bool extract(comm_disc::Disc& disc, const comm_disc::File& f, const std::filesystem::path& out, std::string& why) {
  std::error_code ec;
  if (out.has_parent_path()) std::filesystem::create_directories(out.parent_path(), ec);
  std::ofstream o(out, std::ios::binary);
  if (!o) { why = "cannot write " + out.string(); return false; }
  std::vector<char> buf(1 << 20);
  for (uint64_t at = 0; at < f.size;) {
    const size_t n = size_t(std::min<uint64_t>(buf.size(), f.size - at));
    if (!disc.read(f, at, buf.data(), n, why)) return false;
    o.write(buf.data(), std::streamsize(n));
    at += n;
  }
  return bool(o);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: wii_disc_test <image> list | extract <path> <out> | extractall <dir>\n");
    return 2;
  }
  std::string why;
  // (a plain Wii .iso / .wbfs: with the key file beside it, as the game does -- commentary2)
  const std::filesystem::path image = std::filesystem::absolute(argv[1]);
  auto disc = comm_disc::Disc::open(image, why, comm_disc::find_wii_keys(image.parent_path()));
  if (!disc) { std::fprintf(stderr, "open failed: %s\n", why.c_str()); return 1; }
  const std::string cmd = argv[2];
  if (cmd == "list") {
    std::printf("%s\n", disc->game_id().c_str());
    for (const auto& f : disc->files())
      std::printf("%llu\t%llu\t%s\n", (unsigned long long)f.size, (unsigned long long)f.offset, f.path.c_str());
    return 0;
  }
  if (cmd == "extract" && argc == 5) {
    for (const auto& f : disc->files())
      if (f.path == argv[3]) {
        if (extract(*disc, f, argv[4], why)) return 0;
        std::fprintf(stderr, "read failed: %s\n", why.c_str());
        return 1;
      }
    std::fprintf(stderr, "no such file: %s\n", argv[3]);
    return 1;
  }
  if (cmd == "extractall" && argc == 4) {
    for (const auto& f : disc->files())
      if (!extract(*disc, f, std::filesystem::path(argv[3]) / std::filesystem::path(f.path)  /* disc names are ASCII */, why)) {
        std::fprintf(stderr, "read failed (%s): %s\n", f.path.c_str(), why.c_str());
        return 1;
      }
    return 0;
  }
  std::fprintf(stderr, "bad command\n");
  return 2;
}
