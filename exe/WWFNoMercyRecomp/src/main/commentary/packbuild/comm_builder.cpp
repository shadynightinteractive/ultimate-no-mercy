// [commentary2 2026-11-18] See comm_builder.h. Each clip is made the way tools\commentary\rebuild.py makes it:
//   w13 (a whole line): the .wem decoded as vgmstream-cli writes it (16-bit), silence trimmed off both ends, encoded;
//   cut (a piece of a line): the line's samples [start, start + length) from its trimmed start (start may be negative:
//        before the trim) times gain, linear fades at the ends, truncated to 16 bits (see make_clip), encoded without
//        a trim.
// and judged: identical (the SHA-1 of the canonical clip: clips.tsv's disc_sha1, what this builder makes from the real
// disc -- the user's decision 2026-11-18; the developer's old clip, sha1, when a recipe has no disc_sha1), equivalent
// (rebuild.py's judge on the decoded clip: length within 4096 samples and loudness within 1 dB of the old clip), else
// wrong. The pack takes the identical and equivalent ones (rebuild.py cmd_build).
#include "comm_builder.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

#include "comm_encode.h"
#include "wii_disc.h"
#include "wwise_decode.h"

namespace fs = std::filesystem;

namespace comm_build {

namespace {

// ---- a byte source: a disc image's file or a file on disk ---------------------------------------------------------
struct Where {
    int file = -1;           // index into the source's files
    uint64_t offset = 0;     // in that file
    uint64_t size = 0;
    bool in_bank = false;    // a sound bank's own media (DIDX/DATA): offset/size are the bank's, the id picks inside
};

uint32_t rd32(const uint8_t* p, bool be) {
    return be ? (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]
              : (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// a sound bank's own media: DIDX (id, offset, size) x n into DATA (tools\commentary\rebuild.py bank_media)
bool bank_media(const std::vector<uint8_t>& b, uint32_t want, std::vector<uint8_t>* out, std::vector<uint32_t>* ids) {
    size_t i = 0;
    int be = -1;
    const uint8_t* didx = nullptr; size_t didx_n = 0;
    const uint8_t* data = nullptr; size_t data_n = 0;
    while (i + 8 <= b.size()) {
        if (be < 0) be = rd32(b.data() + i + 4, false) < b.size() ? 0 : 1;   // (the first chunk's size tells the byte order)
        const uint32_t n = rd32(b.data() + i + 4, be == 1);
        if (i + 8 + (uint64_t)n > b.size()) break;
        if (std::memcmp(b.data() + i, "DIDX", 4) == 0) { didx = b.data() + i + 8; didx_n = n; }
        else if (std::memcmp(b.data() + i, "DATA", 4) == 0) { data = b.data() + i + 8; data_n = n; }
        i += 8 + (size_t)n;
    }
    if (!didx || !data) return false;
    for (size_t k = 0; k + 12 <= didx_n; k += 12) {
        const uint32_t id = rd32(didx + k, be == 1), off = rd32(didx + k + 4, be == 1), size = rd32(didx + k + 8, be == 1);
        if (ids) ids->push_back(id);
        if (out && id == want) {
            if ((uint64_t)off + size > data_n) return false;
            out->assign(data + off, data + off + size);
            return true;
        }
    }
    return ids != nullptr;
}

// ---- Media over a list of files (a disc image's, or a folder's) ---------------------------------------------------
class FileMedia : public Media {
public:
    std::vector<std::string> names;       // shown in notes
    std::vector<uint64_t> sizes;
    std::map<uint32_t, Where> where;
    std::string id;

    bool has(uint32_t m) const override { return where.count(m) != 0; }
    size_t count() const override { return where.size(); }
    std::string game_id() const override { return id; }

    bool read(uint32_t m, std::vector<uint8_t>& out, std::string& why) const override {
        auto w = where.find(m);
        if (w == where.end()) { why = "not on this disc"; return false; }
        std::vector<uint8_t> b((size_t)w->second.size);
        if (!read_at(w->second.file, w->second.offset, b.data(), b.size(), why)) return false;
        if (!w->second.in_bank) { out.swap(b); return true; }
        if (!bank_media(b, m, &out, nullptr) || out.empty()) { why = "not found in its sound bank"; return false; }
        return true;
    }

    virtual bool read_at(int file, uint64_t at, uint8_t* dst, size_t n, std::string& why) const = 0;

    // every Wwise file package (.pck, "AKPK") and loose bank / .wem: their media indexed (akpk.py's layout)
    void index() {
        for (int f = 0; f < (int)names.size(); f++) {
            uint8_t h[64] = {};
            std::string why;
            if (sizes[(size_t)f] < 8 || !read_at(f, 0, h, (size_t)std::min<uint64_t>(64, sizes[(size_t)f]), why)) continue;
            if (std::memcmp(h, "AKPK", 4) == 0) index_package(f, h);
        }
        // (media only in sound banks, never seen streamed: looked for only if a package bank holds them -- done in
        //  index_package; loose .bnk files are rare on consoles and not needed by the recipe)
    }

private:
    void index_package(int f, const uint8_t* h) {
        // "AKPK", u32 header size, u32 version 1 (tells the byte order), u32 language map size, bank table size, stream
        // table size, [u32 externals table size: newer Wwise]
        const bool be = rd32(h + 8, false) != 1;
        if (rd32(h + 8, be) != 1) return;
        const uint32_t hsize = rd32(h + 4, be), lmap = rd32(h + 12, be), bnk = rd32(h + 16, be), stm = rd32(h + 20, be);
        uint32_t ext = 0, p = 24;
        if (16ull + lmap + bnk + stm != hsize) {
            ext = rd32(h + 24, be);
            p = 28;
            if (20ull + lmap + bnk + stm + ext != hsize) return;
        }
        if (hsize > (64u << 20) || 8ull + hsize > sizes[(size_t)f]) return;
        std::vector<uint8_t> blob(8 + (size_t)hsize);
        std::string why;
        if (!read_at(f, 0, blob.data(), blob.size(), why)) return;
        p += lmap;
        auto table = [&](uint32_t at, uint32_t len, bool bank, int idsize) {
            if (len < 4 || (uint64_t)at + len > blob.size()) return;
            const uint32_t n = rd32(blob.data() + at, be);
            const uint32_t step = idsize == 8 ? 24 : 20;
            for (uint32_t i = 0; i < n && 4ull + (uint64_t)(i + 1) * step <= len; i++) {
                const uint8_t* e = blob.data() + at + 4 + (size_t)i * step;
                uint64_t fid = idsize == 8 ? ((uint64_t)rd32(e, be) << 32 | rd32(e + 4, be)) : rd32(e, be);
                if (idsize == 8 && !be) fid = (uint64_t)rd32(e + 4, false) << 32 | rd32(e, false);
                const uint8_t* q = e + idsize;
                const uint32_t block = rd32(q, be), size = rd32(q + 4, be), start = rd32(q + 8, be);
                Where w{f, (uint64_t)start * block, size, false};
                if (w.offset + w.size > sizes[(size_t)f]) continue;
                if (!bank) { where.emplace((uint32_t)fid, w); continue; }
                // a bank inside the package: its own media
                std::vector<uint8_t> b((size_t)size);
                std::string why2;
                if (size > (256u << 20) || !read_at(f, w.offset, b.data(), b.size(), why2)) continue;
                std::vector<uint32_t> ids;
                if (bank_media(b, 0, nullptr, &ids)) for (uint32_t id : ids) where.emplace(id, Where{f, w.offset, w.size, true});
            }
        };
        table(p, bnk, true, 4); p += bnk;
        table(p, stm, false, 4); p += stm;
        if (ext) table(p, ext, false, 8);
    }
};

class DiscMedia : public FileMedia {
public:
    fs::path image;
    std::vector<comm_disc::WiiKey> keys;   // (the player's key candidates, for a plain Wii image: in memory only)
    std::vector<comm_disc::File> files;
    mutable std::mutex mtx;
    mutable std::vector<std::unique_ptr<comm_disc::Disc>> pool;   // one Disc per reader at a time

    bool read_at(int file, uint64_t at, uint8_t* dst, size_t n, std::string& why) const override {
        std::unique_ptr<comm_disc::Disc> d;
        {
            std::lock_guard<std::mutex> lk(mtx);
            if (!pool.empty()) { d = std::move(pool.back()); pool.pop_back(); }
        }
        if (!d) d = comm_disc::Disc::open(image, why, keys);
        if (!d) return false;
        const bool ok = d->read(files[(size_t)file], at, dst, n, why);
        std::lock_guard<std::mutex> lk(mtx);
        pool.push_back(std::move(d));
        return ok;
    }
};

class FolderMedia : public FileMedia {
public:
    std::vector<fs::path> paths;
    bool read_at(int file, uint64_t at, uint8_t* dst, size_t n, std::string& why) const override {
        std::ifstream in(paths[(size_t)file], std::ios::binary);
        if (!in) { why = "can't open " + names[(size_t)file]; return false; }
        in.seekg((std::streamoff)at);
        in.read((char*)dst, (std::streamsize)n);
        if ((size_t)in.gcount() != n) { why = "short read in " + names[(size_t)file]; return false; }
        return true;
    }
};

}  // namespace

std::unique_ptr<Media> Media::open(const fs::path& source, std::string& why) {
    std::error_code ec;
    if (fs::is_directory(source, ec)) {
        auto m = std::make_unique<FolderMedia>();
        for (auto it = fs::recursive_directory_iterator(source, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            m->paths.push_back(it->path());
            m->names.push_back(it->path().lexically_relative(source).generic_string());
            m->sizes.push_back((uint64_t)it->file_size(ec));
        }
        m->index();
        if (m->where.empty()) { why = "no Wwise sound packages (.pck) in it"; return nullptr; }
        return m;
    }
    // (a plain Wii .iso / .wbfs needs the Wii key file the player put beside it: roms\, find_wii_keys)
    std::vector<comm_disc::WiiKey> keys = comm_disc::find_wii_keys(source.parent_path());
    std::unique_ptr<comm_disc::Disc> d = comm_disc::Disc::open(source, why, keys);
    if (!d) return nullptr;
    auto m = std::make_unique<DiscMedia>();
    m->image = source;
    m->keys = keys;
    m->id = d->game_id();
    m->files = d->files();
    for (const comm_disc::File& f : m->files) { m->names.push_back(f.path); m->sizes.push_back(f.size); }
    m->pool.push_back(std::move(d));
    m->index();
    if (m->where.empty()) { why = "a disc (" + m->id + ") with no Wwise sound packages (.pck): not WWE '13"; return nullptr; }
    return m;
}

size_t recipe_media_found(const Recipe& r, const Media& m, size_t& needed) {
    std::vector<uint32_t> ids;
    for (const RecipeClip& c : r.clips) if (c.media) ids.push_back(c.media);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    needed = ids.size();
    size_t have = 0;
    for (uint32_t id : ids) have += m.has(id);
    return have;
}

namespace {

// the line as rebuild.py's ffmpeg sees it: vgmstream's 16-bit samples, as floats
bool line_samples(const Media& media, uint32_t id, std::vector<int16_t>& s, int& rate, std::string& why) {
    std::vector<uint8_t> wem;
    if (!media.read(id, wem, why)) return false;
    return wem_decode(wem.data(), wem.size(), s, rate, why);
}

double rms_db(const float* x, size_t n) {
    double acc = 0;
    for (size_t i = 0; i < n; i++) acc += (double)x[i] * x[i];
    return 10.0 * std::log10((n ? acc / (double)n : 0.0) + 1e-12);
}

}  // namespace

std::string make_clip(const Media& media, const Recipe& r, const RecipeClip& c, std::vector<uint8_t>& out, std::string& note) {
    out.clear();
    if (c.cut && !c.placed) { note = "clip " + std::to_string(c.clip) + ": a piece not yet placed in the recipe (its place on the disc is found once with the disc: tools/commentary/trace_disc.py)"; return "missing"; }
    if (!media.has(c.media)) { note = "clip " + std::to_string(c.clip) + ": line " + std::to_string(c.media) + " not on this disc"; return "missing"; }
    std::vector<int16_t> s;
    int rate = 0;
    std::string why;
    if (!line_samples(media, c.media, s, rate, why)) { note = "line " + std::to_string(c.media) + ": " + why; return "wrong"; }
    std::vector<float> x(s.size());
    for (size_t i = 0; i < s.size(); i++) x[i] = (float)s[i] * (1.0f / 32768.0f);
    size_t first = 0, last = 0;
    trim_silence(x, rate, first, last);
    std::vector<float> enc;
    if (!c.cut) {
        enc.assign(x.begin() + (ptrdiff_t)first, x.begin() + (ptrdiff_t)last);
    } else {
        if (rate != r.rate) { note = "line " + std::to_string(c.media) + " is not at " + std::to_string(r.rate) + " Hz"; return "wrong"; }
        // [commdisc 2026-11-18] The pieces as the developer cut them, found exactly on the real disc (all 151 the same
        // samples, tools\commentary\cutfit.py apply): the line's 16-bit samples as floats (v / 32768), times gain (1) and
        // linear fades -- numpy.linspace(0, 1, F) over the first fade_in samples and the same reversed over the last
        // fade_out -- written back to 16 bits as vgmstream does, (int)(f * 32767), i.e. truncated. start counts from the
        // trimmed line's first sample, but the pieces were cut from the line before its trim (61 of the 151 start before
        // it, down to -1337 samples): the samples are the WHOLE line's, zero only outside it. Same order of operations
        // as numpy, in double.
        const int64_t n = c.length, fin = c.fade_in, fout = c.fade_out;
        enc.assign((size_t)std::max<int64_t>(n, 0), 0.0f);
        for (int64_t t = 0; t < n; t++) {
            const int64_t at = (int64_t)first + c.start + t;
            const double v = (at >= 0 && at < (int64_t)s.size()) ? (double)s[(size_t)at] : 0.0;
            double w = 1.0;
            if (fin >= 2 && t < fin) w = (double)t / (double)(fin - 1);
            if (fout >= 2 && t >= n - fout) w = std::min(w, (double)(n - 1 - t) / (double)(fout - 1));
            const double y = std::trunc(v / 32768.0 * c.gain * w * 32767.0);
            const double q = std::min(32767.0, std::max(-32768.0, y));
            enc[(size_t)t] = (float)(int16_t)q * (1.0f / 32768.0f);
        }
    }
    out = encode_clip(enc.data(), enc.size(), rate, r.comment, c.serial);
    if (out.empty()) { note = "the encoder failed"; return "wrong"; }
    const Sha1 h = sha1(out.data(), out.size());
    if (h == (c.has_disc_sha ? c.disc_sha : c.sha)) return "identical";
    // rebuild.py judge: the same line from another encoder build -- length within 4096 samples, loudness within 1 dB.
    // [commdisc 2026-11-18] Judged on the clip DECODED, as rebuild.py does: the recipe's loudness was measured on the
    // decoded clip, and Vorbis q0 adds up to ~1.5 dB to what goes in (measured on the real disc: judging the samples
    // before encoding called 405 good lines "wrong").
    std::vector<float> dec;
    if (!decode_clip(out.data(), out.size(), dec)) { note = "clip " + std::to_string(c.clip) + ": the clip made doesn't decode"; out.clear(); return "wrong"; }
    const double db = rms_db(dec.data(), dec.size());
    if (std::llabs((long long)dec.size() - (long long)c.samples) <= 4096 && std::fabs(db - c.rms) <= 1.0) {
        note = "clip " + std::to_string(c.clip) + ": not the same bytes, the same line (" + std::to_string(dec.size()) + " samples, " +
               std::to_string(db).substr(0, 6) + " dB)";
        return "equivalent";
    }
    note = "clip " + std::to_string(c.clip) + ": " + std::to_string(dec.size()) + " samples / " + std::to_string(db).substr(0, 6) +
           " dB, the recipe says " + std::to_string(c.samples) + " / " + std::to_string(c.rms).substr(0, 6);
    out.clear();
    return "wrong";
}

bool build_pack(const Media& media, const Recipe& r, std::vector<uint8_t>& pack, BuildReport& rep, std::string& why,
                int threads, const std::function<void(size_t, size_t)>& progress, const std::atomic<bool>* stop,
                void (*thread_start)()) {
    const size_t n = r.clips.size();
    std::vector<std::vector<uint8_t>> have(n);
    std::vector<std::string> state(n), note(n);
    std::atomic<size_t> next{0}, done{0};
    auto work = [&]() {
        for (;;) {
            if (stop && stop->load()) return;
            const size_t i = next.fetch_add(1);
            if (i >= n) return;
            state[i] = make_clip(media, r, r.clips[i], have[i], note[i]);
            const size_t d = done.fetch_add(1) + 1;
            if (progress) progress(d, n);
        }
    };
    std::vector<std::thread> pool;
    for (int t = 1; t < std::max(1, threads); t++) pool.emplace_back([&]() { if (thread_start) thread_start(); work(); });
    work();
    for (std::thread& t : pool) t.join();
    if (stop && stop->load()) { why = "stopped"; return false; }
    for (size_t i = 0; i < n; i++) {
        if (state[i] == "identical") rep.identical++;
        else if (state[i] == "equivalent") rep.equivalent++;
        else if (state[i] == "wrong") rep.wrong++;
        else rep.missing++;
    }
    // the notes: the clips left out first (each wrong one, then the missing ones), then a few of the equivalent ones
    for (const char* kind : { "wrong", "missing", "equivalent" })
        for (size_t i = 0; i < n; i++)
            if (state[i] == kind && !note[i].empty() && rep.notes.size() < 60) rep.notes.push_back(state[i] + ": " + note[i]);
    if (rep.identical + rep.equivalent == 0) { why = "no clip could be made from this disc"; return false; }
    pack = pack_from_recipe(r, have, rep.lines);
    rep.clips = rep.identical + rep.equivalent;
    const Sha1 h = sha1(pack.data(), pack.size());
    rep.pack_sha1 = hex(h.data(), h.size());
    rep.same_as_target = rep.pack_sha1 == r.target_sha1;
    return true;
}

}  // namespace comm_build
