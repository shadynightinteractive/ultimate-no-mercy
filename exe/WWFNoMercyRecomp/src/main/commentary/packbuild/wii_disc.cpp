// [arcade] Disc image reader (see wii_disc.h). Written from Dolphin's format description
// docs/WiaAndRvz.md; a few reading details follow Dolphin's Source/Core/DiscIO/WIABlob.cpp and
// WIACompression.cpp (GPL-2.0-or-later; this project is GPL-3.0) and LaggedFibonacciGenerator.cpp
// (CC0) -- each such place says "as Dolphin". No code is copied from them.
//
// Layout in short (all integers big-endian):
//   0x00 file head (0x48 bytes, SHA-1 checked), 0x48 disc struct (SHA-1 checked): disc type, compression,
//   chunk size, the disc's first 0x80 bytes, then where the tables are:
//   partition table (uncompressed, SHA-1 checked): per Wii partition two "data" ranges of sectors;
//   raw data table and group table (compressed like the data): which groups hold which disc range.
//   A group is one chunk of data: [exception lists (Wii partition data only)] [data, maybe RVZ packed].
#include "wii_disc.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <new>

#include "aes128.h"
#include "zstd.h"  // third_party/zstd (decoder only)

namespace comm_disc {
namespace {

uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint64_t be64(const uint8_t* p) { return uint64_t(be32(p)) << 32 | be32(p + 4); }

const char* const kConvert = " Convert it to RVZ in Dolphin (right-click the game > Convert File... > RVZ).";

constexpr uint64_t kSector = 0x8000;       // one Wii disc sector
constexpr uint64_t kSectorData = 0x7C00;   // its data without the hash block
constexpr uint64_t kWiiGroup = 0x200000;   // 64 sectors: one hash group (one exception list each)
constexpr size_t kExceptionSize = 22;      // u16 offset + 20-byte SHA-1

// SHA-1, only to check the image's header tables are intact.
std::array<uint8_t, 20> sha1(const uint8_t* data, size_t n) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  auto rol = [](uint32_t x, int s) { return x << s | x >> (32 - s); };
  std::vector<uint8_t> m(data, data + n);
  m.push_back(0x80);
  while (m.size() % 64 != 56) m.push_back(0);
  for (int i = 7; i >= 0; --i) m.push_back(uint8_t(uint64_t(n) * 8 >> (i * 8)));
  for (size_t b = 0; b < m.size(); b += 64) {
    uint32_t w[80];
    for (size_t i = 0; i < 16; ++i) w[i] = be32(&m[b + i * 4]);
    for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
      uint32_t f, k;
      if (i < 20) { f = (bb & c) | (~bb & d); k = 0x5A827999; }
      else if (i < 40) { f = bb ^ c ^ d; k = 0x6ED9EBA1; }
      else if (i < 60) { f = (bb & c) | (bb & d) | (c & d); k = 0x8F1BBCDC; }
      else { f = bb ^ c ^ d; k = 0xCA62C1D6; }
      uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rol(bb, 30); bb = a; a = t;
    }
    h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e;
  }
  std::array<uint8_t, 20> out;
  for (size_t i = 0; i < 20; ++i) out[i] = uint8_t(h[i / 4] >> (24 - i % 4 * 8));
  return out;
}

bool hash_ok(const uint8_t* data, size_t n, const uint8_t* expected) {
  return std::memcmp(sha1(data, n).data(), expected, 20) == 0;
}

// The image file, read at any offset.
class ImageFile {
public:
  bool open(const std::filesystem::path& p) {
    std::error_code ec;
    size_ = std::filesystem::file_size(p, ec);
    if (ec) return false;
    f_.open(p, std::ios::binary);
    return f_.is_open();
  }
  uint64_t size() const { return size_; }
  bool read_at(uint64_t off, void* dst, size_t n) {
    if (off > size_ || n > size_ - off) return false;
    f_.clear();
    f_.seekg(std::streamoff(off));
    f_.read(static_cast<char*>(dst), std::streamsize(n));
    return f_.gcount() == std::streamsize(n);
  }
private:
  std::ifstream f_;
  uint64_t size_ = 0;
};

// The disc's junk-data generator (lagged Fibonacci, xor, j = 32, k = 521), as the spec's
// "PRNG algorithm" section describes. RVZ stores a 68-byte seed instead of the junk itself.
class JunkGenerator {
public:
  void seed(const uint8_t* s) {
    for (int i = 0; i < 17; ++i) w_[i] = be32(s + i * 4);
    for (int i = 17; i < 521; ++i) w_[i] = (w_[i - 17] << 23) ^ (w_[i - 16] >> 9) ^ w_[i - 1];
    for (int i = 0; i < 4; ++i) advance();
    pos_ = 0;
  }
  void skip(uint64_t bytes) {
    pos_ += bytes;
    while (pos_ >= kBytes) { advance(); pos_ -= kBytes; }
  }
  void fill(uint8_t* out, size_t n) {
    static const int shift[4] = {24, 18, 8, 0};  // byte 1 is ">> 18", not 16: how the discs were made
    for (size_t i = 0; i < n; ++i) {
      out[i] = uint8_t(w_[pos_ / 4] >> shift[pos_ % 4]);
      if (++pos_ == kBytes) { advance(); pos_ = 0; }
    }
  }
private:
  static constexpr uint64_t kBytes = 521 * 4;
  void advance() {
    for (int i = 0; i < 32; ++i) w_[i] ^= w_[i + 521 - 32];
    for (int i = 32; i < 521; ++i) w_[i] ^= w_[i - 32];
  }
  uint32_t w_[521] = {};
  uint64_t pos_ = 0;
};

// A run of data held by consecutive groups: a raw range of the disc, or one data range of the Wii
// game partition (then in decrypted-data units: 0x7C00 bytes per sector).
struct Range {
  uint64_t start = 0, size = 0;   // start already rounded down to a sector, as the spec says
  uint32_t first_group = 0, groups = 0;
  uint64_t chunk = 0;             // data bytes per group
  uint32_t exception_lists = 0;   // per group: 0 for raw data
};

// A plain, unencrypted GameCube image: the disc is the file.
class PlainDisc final : public Disc {
public:
  bool init(const std::filesystem::path& p, std::string& why) {
    if (!file_.open(p)) { why = "cannot open the file"; return false; }
    char id[6];
    if (!file_.read_at(0, id, 6)) { why = "the file is too short"; return false; }
    game_id_.assign(id, 6);
    return load_file_system(0, why);
  }
protected:
  bool read_data(uint64_t off, uint8_t* dst, size_t n, std::string& why) override {
    if (file_.read_at(off, dst, n)) return true;
    why = "read past the end of the image (is it cut short?)";
    return false;
  }
private:
  ImageFile file_;
};

// [commentary2 2026-11-18] A plain Wii image -- a 1:1 .iso, or a .wbfs (Wii Backup File System: the disc in blocks of
// 2^wbfs_sector_shift bytes and a table of where each is stored; laid out as Dolphin's WbfsBlob.cpp reads it) -- whose
// game partition is encrypted, read with the Wii common key the PLAYER supplied (find_wii_keys). The partition's ticket
// holds the disc's title key, encrypted with the common key (AES-128-CBC, IV = the title ID + 8 zero bytes); each
// 0x8000-byte sector of the partition data is a 0x400-byte hash block then 0x7C00 bytes of data, AES-128-CBC with the
// title key, the data's IV being bytes 0x3D0-0x3DF of the (encrypted) hash block. The keys are never logged or kept.
class EncryptedWiiDisc final : public Disc {
public:
  bool init(const std::filesystem::path& p, bool wbfs, const std::vector<WiiKey>& keys, std::string& why) {
    if (!file_.open(p)) { why = "cannot open the file"; return false; }
    if (wbfs && !init_wbfs(why)) return false;
    uint8_t h[0x80];
    if (!read_disc(0, h, sizeof h)) { why = "the image is too short"; return false; }
    game_id_.assign(reinterpret_cast<char*>(h), 6);
    // the partition tables (0x40000: 4 x (count, table offset >> 2)); the game partition is type 0
    uint8_t pt[32];
    if (!read_disc(0x40000, pt, sizeof pt)) { why = "damaged image (partition tables)"; return false; }
    uint64_t part = 0;
    for (int t = 0; t < 4 && !part; ++t) {
      const uint32_t count = be32(pt + 8 * t);
      const uint64_t at = uint64_t(be32(pt + 8 * t + 4)) << 2;
      for (uint32_t i = 0; i < count && i < 64 && !part; ++i) {
        uint8_t e[8];
        if (!read_disc(at + 8 * i, e, 8)) { why = "damaged image (partition table)"; return false; }
        if (be32(e + 4) == 0) part = uint64_t(be32(e)) << 2;
      }
    }
    if (!part) { why = "no game partition on this Wii disc"; return false; }
    uint8_t ph[0x2C0];
    if (!read_disc(part, ph, sizeof ph)) { why = "damaged image (partition header)"; return false; }
    data_ = part + (uint64_t(be32(ph + 0x2B8)) << 2);
    data_size_ = uint64_t(be32(ph + 0x2BC)) << 2;
    uint8_t iv[16] = {};
    std::memcpy(iv, ph + 0x1DC, 8);                                   // the title ID
    for (const WiiKey& k : keys) {
      uint8_t title_key[16];
      std::memcpy(title_key, ph + 0x1BF, 16);                         // the title key, encrypted with the common key
      Aes128(k.data()).cbc_decrypt(title_key, 16, iv);
      aes_.reset(new Aes128(title_key));
      std::memset(title_key, 0, sizeof title_key);
      cached_ = UINT64_MAX;
      uint8_t boot[0x20];
      std::string w;
      if (read_data(0, boot, sizeof boot, w) && be32(boot + 0x18) == 0x5D1C9EA3) return load_file_system(2, why);
    }
    aes_.reset();
    why = keys.empty() ? std::string("This Wii disc image is encrypted: put your Wii's key file (keys.bin as BootMii dumps it, "
                                     "or common-key.bin) in roms\\ beside it -- or convert it to RVZ in Dolphin (right-click the "
                                     "game > Convert File... > RVZ).")
                       : std::string("The Wii key file in roms\\ does not open this disc (not the Wii common key?).") + kConvert;
    return false;
  }

protected:
  bool read_data(uint64_t off, uint8_t* dst, size_t n, std::string& why) override {
    while (n) {
      const uint64_t sector = off / kSectorData, in = off % kSectorData;
      if ((sector + 1) * kSector > data_size_) { why = "read past the end of the game partition"; return false; }
      if (sector != cached_) {
        if (!read_disc(data_ + sector * kSector, raw_, kSector)) { why = "read past the end of the image (is it cut short?)"; return false; }
        std::memcpy(plain_, raw_ + 0x400, kSectorData);
        aes_->cbc_decrypt(plain_, kSectorData, raw_ + 0x3D0);
        cached_ = sector;
      }
      const size_t take = size_t(std::min<uint64_t>(n, kSectorData - in));
      std::memcpy(dst, plain_ + in, take);
      dst += take; off += take; n -= take;
    }
    return true;
  }

private:
  bool init_wbfs(std::string& why) {
    uint8_t h[12];
    if (!file_.read_at(0, h, sizeof h)) { why = "damaged .wbfs file"; return false; }
    const int hd_shift = h[8], wbfs_shift = h[9];
    if (hd_shift < 9 || hd_shift > 16 || wbfs_shift < 15 || wbfs_shift > 30) { why = "damaged .wbfs file (sector sizes)"; return false; }
    wbfs_shift_ = wbfs_shift;
    const uint64_t blocks = (uint64_t(143432) * 2 * kSector) >> wbfs_shift;   // a dual-layer disc's worth (as Dolphin)
    std::vector<uint8_t> t(size_t(blocks) * 2);
    if (!file_.read_at((uint64_t(1) << hd_shift) + 0x100, t.data(), t.size())) { why = "damaged .wbfs file (block table)"; return false; }
    wlba_.resize(size_t(blocks));
    for (size_t i = 0; i < wlba_.size(); ++i) wlba_[i] = be16(&t[2 * i]);
    return true;
  }
  // the disc's bytes: the file itself (.iso), or through the block table (.wbfs; a block not stored reads as zeros)
  bool read_disc(uint64_t off, uint8_t* dst, size_t n) {
    if (wlba_.empty()) return file_.read_at(off, dst, n);
    while (n) {
      const uint64_t block = off >> wbfs_shift_, in = off & ((uint64_t(1) << wbfs_shift_) - 1);
      const size_t take = size_t(std::min<uint64_t>(n, (uint64_t(1) << wbfs_shift_) - in));
      if (block >= wlba_.size()) return false;
      if (wlba_[size_t(block)] == 0) std::memset(dst, 0, take);
      else if (!file_.read_at((uint64_t(wlba_[size_t(block)]) << wbfs_shift_) + in, dst, take)) return false;
      dst += take; off += take; n -= take;
    }
    return true;
  }
  ImageFile file_;
  std::vector<uint16_t> wlba_;
  int wbfs_shift_ = 0;
  uint64_t data_ = 0, data_size_ = 0;
  std::unique_ptr<Aes128> aes_;
  uint64_t cached_ = UINT64_MAX;
  uint8_t raw_[kSector];
  uint8_t plain_[kSectorData];
};


// An RVZ or WIA image.
class WiaDisc final : public Disc {
public:
  explicit WiaDisc(bool rvz) : rvz_(rvz) {}
  ~WiaDisc() override { ZSTD_freeDCtx(zstd_); }
  bool init(const std::filesystem::path& p, std::string& why);
protected:
  bool read_data(uint64_t off, uint8_t* dst, size_t n, std::string& why) override {
    return wii_ ? read_ranges(partition_, off, dst, n, why) : read_disc(off, dst, n, why);
  }
private:
  struct Group { uint32_t off4, size, packed_size; };
  bool read_disc(uint64_t off, uint8_t* dst, size_t n, std::string& why);
  bool read_ranges(const std::vector<Range>& ranges, uint64_t off, uint8_t* dst, size_t n, std::string& why);
  const std::vector<uint8_t>* group_data(const Range& r, uint32_t index, std::string& why);
  bool unzstd(const std::vector<uint8_t>& in, size_t limit, std::vector<uint8_t>& out);
  bool read_table(uint64_t off, uint32_t stored, size_t want, std::vector<uint8_t>& out);
  bool find_game_partition(const std::vector<uint8_t>& parts, uint32_t count, uint32_t entry_size, std::string& why);

  bool rvz_, wii_ = false, compressed_ = false;
  ImageFile file_;
  uint8_t disc_head_[0x80] = {};
  uint64_t chunk_ = 0;
  std::vector<Group> groups_;
  std::vector<Range> raw_, partition_;
  ZSTD_DCtx* zstd_ = nullptr;
  int64_t cached_group_ = -1;           // the last decoded group, kept for the next small read
  std::vector<uint8_t> cache_;
};

bool WiaDisc::init(const std::filesystem::path& p, std::string& why) {
  const char* kind = rvz_ ? "RVZ" : "WIA";
  auto damaged = [&](const char* what) { why = std::string("damaged ") + kind + " file (" + what + ")"; return false; };
  if (!file_.open(p)) { why = "cannot open the file"; return false; }

  // File head: magic, versions, the disc struct's size and hash, sizes, own hash.
  uint8_t head[0x48];
  if (!file_.read_at(0, head, sizeof head)) return damaged("too short");
  if (!hash_ok(head, 0x34, head + 0x34)) return damaged("header");
  const uint32_t version = be32(head + 4), compatible = be32(head + 8);
  // Versions accepted as Dolphin does: it reads WIA from 0.08 and RVZ from 0.03, both up to 1.00.
  if (compatible > 0x01000000 || version < (rvz_ ? 0x00030000u : 0x00080000u)) {
    why = std::string("unsupported ") + kind + " version; convert it again with a current Dolphin";
    return false;
  }
  if (be64(head + 0x2C) != file_.size()) return damaged("wrong size: cut short?");

  // Disc struct.
  const uint32_t disc_size = be32(head + 0x0C);
  if (disc_size < 0xD5 || disc_size > 0x10000) return damaged("disc struct");
  std::vector<uint8_t> d(std::max<uint32_t>(disc_size, 0xDC), 0);
  if (!file_.read_at(0x48, d.data(), disc_size) ||
      !hash_ok(d.data(), disc_size, head + 0x10))
    return damaged("disc struct");
  const uint32_t disc_type = be32(&d[0]), compression = be32(&d[4]), chunk = be32(&d[0x0C]);
  std::memcpy(disc_head_, &d[0x10], 0x80);
  game_id_.assign(reinterpret_cast<const char*>(disc_head_), 6);
  wii_ = disc_type == 2;
  if (disc_type != 1 && disc_type != 2) return damaged("unknown disc type");

  // Compression: none, or Zstandard (RVZ only). The others are refused with a way out.
  static const char* const names[] = {"none", "purge", "bzip2", "LZMA", "LZMA2", "Zstandard"};
  if (compression > 5 || (compression == 5 && !rvz_)) return damaged("unknown compression");
  if (compression != 0 && compression != 5) {
    why = std::string("this ") + kind + " file uses " + names[compression] +
          " compression, which is not supported. Convert it to RVZ with Zstandard compression in Dolphin "
          "(right-click the game > Convert File...).";
    return false;
  }
  compressed_ = compression == 5;
  if (compressed_ && !(zstd_ = ZSTD_createDCtx())) { why = "out of memory"; return false; }

  // Chunk size: a multiple of 2 MiB, or for RVZ also a power of two from 32 KiB.
  const bool pow2 = chunk >= kSector && (chunk & (chunk - 1)) == 0;
  if (chunk == 0 || (chunk % kWiiGroup != 0 && !(rvz_ && pow2))) return damaged("chunk size");
  if (chunk > 0x4000000) return damaged("chunk size over 64 MiB");  // far above what Dolphin or wit write
  chunk_ = chunk;

  // Partition table (uncompressed).
  const uint32_t n_parts = be32(&d[0x90]), part_size = be32(&d[0x94]);
  const uint64_t parts_off = be64(&d[0x98]);
  if (n_parts > 64 || part_size > 0x1000) return damaged("partition table");
  std::vector<uint8_t> parts(size_t(n_parts) * part_size);
  if (!file_.read_at(parts_off, parts.data(), parts.size()) ||
      !hash_ok(parts.data(), parts.size(), &d[0xA0]))
    return damaged("partition table");

  // Raw data table: disc ranges stored as they are (rounded down to a sector, as the spec says).
  std::vector<uint8_t> t;
  const uint32_t n_raw = be32(&d[0xB4]);
  if (n_raw > 0x10000 || !read_table(be64(&d[0xB8]), be32(&d[0xC0]), size_t(n_raw) * 0x18, t)) return damaged("raw data table");
  for (uint32_t i = 0; i < n_raw; ++i) {
    const uint8_t* e = &t[i * 0x18];
    Range r;
    r.start = be64(e);
    r.size = be64(e + 8);
    if (r.size == 0) continue;
    if (r.size > ~uint64_t(0) - r.start) return damaged("raw data table");
    r.size += r.start % kSector;
    r.start -= r.start % kSector;
    r.first_group = be32(e + 0x10);
    r.groups = be32(e + 0x14);
    r.chunk = chunk_;
    raw_.push_back(r);
  }
  // As Dolphin: a disc offset belongs to the range that ends first after it.
  std::sort(raw_.begin(), raw_.end(), [](const Range& a, const Range& b) { return a.start + a.size < b.start + b.size; });

  // Group table.
  const uint32_t n_groups = be32(&d[0xC4]);
  const size_t gsize = rvz_ ? 12 : 8;
  if (n_groups > 0x100000 ||  // a whole dual-layer disc in 32 KiB chunks is 0x44000
      !read_table(be64(&d[0xC8]), be32(&d[0xD0]), n_groups * gsize, t)) return damaged("group table");
  groups_.resize(n_groups);
  for (uint32_t i = 0; i < n_groups; ++i)
    groups_[i] = {be32(&t[i * gsize]), be32(&t[i * gsize + 4]), rvz_ ? be32(&t[i * gsize + 8]) : 0};

  if (wii_ && !find_game_partition(parts, n_parts, part_size, why)) return false;
  return load_file_system(wii_ ? 2 : 0, why);
}

// Reads one table (raw data or groups): stored uncompressed, or as one zstd frame.
bool WiaDisc::read_table(uint64_t off, uint32_t stored, size_t want, std::vector<uint8_t>& out) {
  if (want == 0) { out.clear(); return true; }
  if (off > file_.size() || stored > file_.size() - off) return false;  // check before allocating
  std::vector<uint8_t> in(stored);
  if (!file_.read_at(off, in.data(), in.size())) return false;
  if (!compressed_) { out.swap(in); return out.size() == want; }
  return unzstd(in, want, out) && out.size() == want;
}

// Decompresses one whole zstd stream; fails if it does not end cleanly or would exceed `limit`.
bool WiaDisc::unzstd(const std::vector<uint8_t>& in, size_t limit, std::vector<uint8_t>& out) {
  out.resize(limit);
  ZSTD_DCtx_reset(zstd_, ZSTD_reset_session_only);
  ZSTD_inBuffer src{in.data(), in.size(), 0};
  ZSTD_outBuffer dst{out.data(), out.size(), 0};
  for (;;) {
    const size_t in_before = src.pos, out_before = dst.pos;
    const size_t left = ZSTD_decompressStream(zstd_, &dst, &src);
    if (ZSTD_isError(left)) return false;
    if (left == 0) break;  // the frame is complete
    if (src.pos == in_before && dst.pos == out_before) return false;  // cut short, or more than `limit`
  }
  if (src.pos != src.size) return false;  // something after the frame
  out.resize(dst.pos);
  return true;
}

// Finds the Wii game partition (type 0) in the disc's partition tables and the WIA entry that
// holds its decrypted data.
bool WiaDisc::find_game_partition(const std::vector<uint8_t>& parts, uint32_t count, uint32_t entry_size, std::string& why) {
  // Four partition tables at 0x40000: count, offset >> 2. Each entry: partition offset >> 2, type.
  uint8_t tables[32];
  if (!read_disc(0x40000, tables, sizeof tables, why)) return false;
  uint64_t game = 0;
  for (int t = 0; t < 4 && !game; ++t) {
    const uint32_t n = std::min<uint32_t>(be32(tables + t * 8), 64);
    std::vector<uint8_t> e(n * 8u);
    if (n && !read_disc(uint64_t(be32(tables + t * 8 + 4)) << 2, e.data(), e.size(), why)) return false;
    for (uint32_t i = 0; i < n && !game; ++i)
      if (be32(&e[i * 8 + 4]) == 0) game = uint64_t(be32(&e[i * 8])) << 2;
  }
  if (!game) { why = "no game partition on this Wii disc"; return false; }
  // The partition header (stored as raw data) says where its encrypted data starts (>> 2).
  uint8_t data_off[4];
  if (!read_disc(game + 0x2B8, data_off, 4, why)) return false;
  const uint64_t data_start = game + (uint64_t(be32(data_off)) << 2);

  for (uint32_t i = 0; i < count; ++i) {
    uint8_t e[0x30] = {};  // a shorter entry is padded with zeroes, as the spec says
    std::memcpy(e, &parts[size_t(i) * entry_size], std::min<size_t>(entry_size, sizeof e));
    const uint32_t base = be32(e + 0x10);
    if (base * kSector != data_start) continue;
    for (int j = 0; j < 2; ++j) {
      const uint8_t* pd = e + 0x10 + j * 0x10;
      Range r;
      if (be32(pd + 4) == 0) continue;
      if (be32(pd) < base) { why = "damaged image (partition ranges)"; return false; }
      r.start = (be32(pd) - base) * kSectorData;
      r.size = be32(pd + 4) * kSectorData;
      r.first_group = be32(pd + 8);
      r.groups = be32(pd + 12);
      r.chunk = chunk_ / kSector * kSectorData;
      r.exception_lists = uint32_t(std::max<uint64_t>(1, chunk_ / kWiiGroup));
      partition_.push_back(r);
    }
    if (partition_.empty()) { why = "the game partition is empty"; return false; }
    return true;
  }
  why = std::string("this image does not hold the game partition decrypted.") + kConvert;
  return false;
}

// Reads disc bytes (raw data ranges; the first 0x80 bytes come from the disc struct).
bool WiaDisc::read_disc(uint64_t off, uint8_t* dst, size_t n, std::string& why) {
  if (off < 0x80) {
    const size_t k = size_t(std::min<uint64_t>(0x80 - off, n));
    std::memcpy(dst, disc_head_ + off, k);
    off += k; dst += k; n -= k;
  }
  return read_ranges(raw_, off, dst, n, why);
}

bool WiaDisc::read_ranges(const std::vector<Range>& ranges, uint64_t off, uint8_t* dst, size_t n, std::string& why) {
  while (n > 0) {
    // The first range ending after `off` (ranges are sorted by end); it must also start at or before it.
    const Range* r = nullptr;
    for (const Range& c : ranges)
      if (c.start + c.size > off) { r = &c; break; }
    if (!r || r->start > off) { why = "the image has no data at that place"; return false; }
    const uint64_t in_range = off - r->start;
    if (in_range / r->chunk >= r->groups) { why = "damaged image (too few groups)"; return false; }
    const uint32_t g = uint32_t(in_range / r->chunk);
    const std::vector<uint8_t>* data = group_data(*r, g, why);
    if (!data) return false;
    const uint64_t at = in_range - uint64_t(g) * r->chunk;
    const size_t k = size_t(std::min<uint64_t>(data->size() - at, n));
    std::memcpy(dst, data->data() + at, k);
    off += k; dst += k; n -= k;
  }
  return true;
}

// Decodes group `index` of range r (all of it) and returns its data; kept until the next group.
const std::vector<uint8_t>* WiaDisc::group_data(const Range& r, uint32_t index, std::string& why) {
  const uint64_t global = uint64_t(r.first_group) + index;
  if (index >= r.groups || global >= groups_.size()) { why = "damaged image (group index)"; return nullptr; }
  if (int64_t(global) == cached_group_) return &cache_;
  cached_group_ = -1;
  auto damaged = [&](const char* what) { why = std::string("damaged image (group ") + std::to_string(global) + ": " + what + ")"; return nullptr; };

  const Group& g = groups_[global];
  const uint64_t offset_in_range = uint64_t(index) * r.chunk;
  const size_t out_size = size_t(std::min(r.chunk, r.size - offset_in_range));  // the last group may be short
  // RVZ: the top bit of the size says the group is compressed; without it the group is stored plainly.
  const bool compressed = compressed_ && (!rvz_ || (g.size & 0x80000000u));
  const uint32_t stored = rvz_ ? g.size & 0x7FFFFFFFu : g.size;

  cache_.assign(out_size, 0);
  if (stored == 0) { cached_group_ = int64_t(global); return &cache_; }  // size 0: all zeroes

  const uint64_t at = uint64_t(g.off4) << 2;
  if (at > file_.size() || stored > file_.size() - at) return damaged("past the end of the file");  // before allocating
  std::vector<uint8_t> in(stored), raw;
  if (!file_.read_at(at, in.data(), stored)) return damaged("unreadable");
  if (compressed) {
    // Room for the most hash exceptions a list may have (3328, as Dolphin allows) plus the data.
    const size_t limit = r.exception_lists * (2 + 3328 * kExceptionSize) + (g.packed_size ? g.packed_size : out_size);
    if (!unzstd(in, limit, raw)) return damaged("zstd data");
  } else {
    raw.swap(in);
  }

  // Skip the exception lists (hash fixes; not needed since we never rebuild the hashes).
  // Stored plainly, they are padded to 4 bytes; compressed, they are not.
  size_t p = 0;
  for (uint32_t i = 0; i < r.exception_lists; ++i) {
    if (raw.size() - p < 2) return damaged("exception list");
    p += 2 + be16(&raw[p]) * kExceptionSize;
    if (p > raw.size()) return damaged("exception list");
  }
  if (r.exception_lists && !compressed) p = std::min((p + 3) & ~size_t(3), raw.size());
  const uint8_t* src = raw.data() + p;
  const size_t src_size = raw.size() - p;

  if (g.packed_size == 0) {  // plain data
    if (src_size != out_size) return damaged("size");
    std::memcpy(cache_.data(), src, out_size);
  } else {
    // RVZ packing: runs of u32 size; top bit clear = that many bytes follow, set = a 68-byte seed
    // follows and the generator makes the bytes. As Dolphin, the generator is moved on by the run's
    // place within its 0x8000-byte sector, counted from the start of the range (for Wii partition
    // data in 0x7C00-byte sectors' data, yet still modulo 0x8000, as the spec says).
    if (src_size != g.packed_size) return damaged("packed size");
    size_t q = 0, o = 0;
    JunkGenerator junk;
    while (q < src_size) {
      if (src_size - q < 4) return damaged("packing");
      const uint32_t v = be32(src + q);
      const size_t len = v & 0x7FFFFFFFu;
      q += 4;
      if (len > out_size - o) return damaged("packing overruns");
      if (v & 0x80000000u) {
        if (src_size - q < 68) return damaged("packing");
        junk.seed(src + q);
        junk.skip((offset_in_range + o) % kSector);
        junk.fill(cache_.data() + o, len);
        q += 68;
      } else {
        if (src_size - q < len) return damaged("packing");
        std::memcpy(cache_.data() + o, src + q, len);
        q += len;
      }
      o += len;
    }
    if (o != out_size) return damaged("packing too short");
  }
  cached_group_ = int64_t(global);
  return &cache_;
}

}  // namespace

Disc::~Disc() = default;

// [commentary2 2026-11-18] The player's Wii key candidates in a folder (see wii_disc.h). Nothing is logged or written.
std::vector<WiiKey> find_wii_keys(const std::filesystem::path& folder) {
  std::vector<WiiKey> out;
  std::error_code ec;
  auto add = [&](const uint8_t* k) {
    WiiKey w;
    std::memcpy(w.data(), k, 16);
    if (std::find(out.begin(), out.end(), w) == out.end()) out.push_back(w);
  };
  for (const auto& e : std::filesystem::directory_iterator(folder, ec)) {
    if (!e.is_regular_file(ec)) continue;
    std::string name = e.path().filename().string();
    for (char& c : name) c = char(tolower((unsigned char)c));
    const bool bootmii = name == "keys.bin";
    const bool named = name == "common-key.bin" || name == "common_key.bin" || name == "commonkey.bin" || name == "ckey.bin" ||
                       name == "common-key.txt" || name == "common_key.txt" || name == "commonkey.txt" || name == "ckey.txt";
    if (!bootmii && !named) continue;
    const uint64_t size = e.file_size(ec);
    ImageFile f;
    if (ec || size > 4096 || !f.open(e.path())) continue;
    std::vector<uint8_t> b(static_cast<size_t>(size));
    if (!b.empty() && !f.read_at(0, b.data(), b.size())) continue;
    if (bootmii) { if (b.size() >= 0x124) add(&b[0x114]); continue; }   // BootMii's dump: the common key at 0x114 (as Dolphin's IOSC reads it)
    if (b.size() == 16) { add(b.data()); continue; }                     // the 16 bytes
    // text: the first 32 hex digits (spaces, "0x", line ends between bytes allowed)
    uint8_t k[16] = {};
    int got = 0;
    for (size_t i = 0; i < b.size() && got < 32; ++i) {
      const uint8_t c = b[i];
      if (c == '0' && i + 1 < b.size() && (b[i + 1] == 'x' || b[i + 1] == 'X') && got % 2 == 0) { ++i; continue; }
      const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
      if (v < 0) { if (got % 2) break; continue; }
      if (got % 2 == 0) k[got / 2] = uint8_t(v << 4); else k[got / 2] |= uint8_t(v);
      ++got;
    }
    if (got == 32) add(k);
  }
  return out;
}

bool Disc::read(const File& f, uint64_t at, void* dst, size_t n, std::string& why) {
  if (at > f.size || n > f.size - at) { why = "read past the end of " + f.path; return false; }
  try {
    return read_data(f.offset + at, static_cast<uint8_t*>(dst), n, why);
  } catch (const std::bad_alloc&) {
    why = "out of memory";
  } catch (...) {
    why = "unexpected error reading the image";
  }
  return false;
}

// The boot header holds the FST's place (0x424) and size (0x428). The FST: 12-byte entries, entry 0
// the root (its size field = entry count), then the names. Each entry: u8 is-directory, u24 name
// offset, then for a file u32 offset (>> shift) and size; for a directory its parent and the index
// just past its last child.
bool Disc::load_file_system(int shift, std::string& why) {
  File boot{"(boot header)", 0, 0x440};
  uint8_t h[0x440];
  if (!read(boot, 0, h, sizeof h, why)) return false;
  const uint64_t fst_off = uint64_t(be32(h + 0x424)) << shift, fst_size = uint64_t(be32(h + 0x428)) << shift;
  if (fst_size < 12 || fst_size > 0x4000000) { why = "damaged file system table"; return false; }
  std::vector<uint8_t> fst(static_cast<size_t>(fst_size));
  File fst_file{"(file system table)", fst_off, fst_size};
  if (!read(fst_file, 0, fst.data(), fst.size(), why)) return false;
  const uint32_t count = be32(&fst[8]);
  if (count == 0 || uint64_t(count) * 12 > fst_size) { why = "damaged file system table"; return false; }
  const char* names = reinterpret_cast<const char*>(&fst[count * size_t(12)]);
  const size_t names_size = fst_size - count * size_t(12);

  std::vector<std::pair<uint32_t, std::string>> dirs{{count, ""}};  // (end index, path prefix)
  for (uint32_t i = 1; i < count; ++i) {
    while (dirs.size() > 1 && dirs.back().first <= i) dirs.pop_back();
    const uint8_t* e = &fst[i * size_t(12)];
    const uint32_t name_off = be32(e) & 0xFFFFFF;
    if (name_off >= names_size) { why = "damaged file system table (name)"; return false; }
    const std::string name(names + name_off, strnlen(names + name_off, names_size - name_off));
    if (e[0]) {
      const uint32_t end = be32(e + 8);
      if (end <= i || end > dirs.back().first) { why = "damaged file system table (directory)"; return false; }
      dirs.push_back({end, dirs.back().second + name + "/"});
    } else {
      files_.push_back({dirs.back().second + name, uint64_t(be32(e + 4)) << shift, be32(e + 8)});
    }
  }
  return true;
}

std::unique_ptr<Disc> Disc::open(const std::filesystem::path& image, std::string& why, const std::vector<WiiKey>& keys) {
  try {
    ImageFile f;
    if (!f.open(image)) { why = "cannot open the file"; return nullptr; }
    uint8_t h[0x210] = {};
    if (!f.read_at(0, h, std::min<uint64_t>(sizeof h, f.size()))) { why = "cannot read the file"; return nullptr; }
    const std::string magic(reinterpret_cast<char*>(h), 4);
    if (magic == std::string("WIA\x01", 4) || magic == std::string("RVZ\x01", 4)) {
      auto d = std::make_unique<WiaDisc>(magic[0] == 'R');
      if (!d->init(image, why)) return nullptr;
      return d;
    }
    const bool wii = be32(h + 0x18) == 0x5D1C9EA3, gamecube = be32(h + 0x1C) == 0xC2339F3D;
    if (std::memcmp(h + 0x200, "NKIT", 4) == 0) { why = std::string("NKit images are not supported.") + kConvert; return nullptr; }
    if (magic == "WBFS" || wii) {
      // (commentary2: with the player's key file -- or the reason to add one / convert)
      auto d = std::make_unique<EncryptedWiiDisc>();
      if (!d->init(image, magic == "WBFS", keys, why)) return nullptr;
      return d;
    }
    if (magic == "CISO" || be32(h) == 0x01C00BB1) { why = std::string("This compressed image type is not supported.") + kConvert; return nullptr; }
    if (gamecube) {
      auto d = std::make_unique<PlainDisc>();
      if (!d->init(image, why)) return nullptr;
      return d;
    }
    why = "not a disc image this reader knows (expected .rvz or .wia)";
  } catch (const std::bad_alloc&) {
    why = "out of memory";
  } catch (...) {
    why = "unexpected error opening the image";
  }
  return nullptr;
}

}  // namespace comm_disc
