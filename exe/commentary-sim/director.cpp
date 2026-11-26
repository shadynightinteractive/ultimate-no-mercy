// [arcade] Match commentary: the director (see director.h).
#include <cmath>
#include "director.h"
#include <cstring>
#include <cstdlib>

namespace comm {

namespace {

struct Rule {
    const char* name;
    int prio;        // 0..9: a higher one may cut a lower one off (by 3 or more), never the reverse
    float chance;    // at talk level 2, the chance the moment gets a line when nothing is playing
    int cooldown;    // ms before the same event gets another line
    int fresh;       // ms an event may wait for the current line to finish (0: now or never)
};

const Rule rules[EV_COUNT_] = {
    { "none",          0, 0.00f,     0,    0 },
    { "match_start",   7, 1.00f, 60000,  4000 },
    { "lockup",        2, 0.35f, 20000,    0 },
    { "strike",        1, 0.18f,  6000,    0 },
    { "strike_big",    3, 0.40f,  8000,    0 },
    { "grapple",       2, 0.30f,  7000,    0 },
    { "big_move",      5, 0.70f,  5000,  700 },
    { "knockdown",     3, 0.35f,  8000,    0 },
    { "reversal",      4, 0.50f,  9000,    0 },
    { "special",       5, 0.80f, 30000, 2500 },
    { "finisher",      8, 1.00f,  4000, 1500 },
    { "pin",           4, 0.45f,  8000,    0 },
    { "count",         9, 1.00f,     0,    0 },
    { "kickout_1",     8, 0.85f,  6000,  800 },
    { "kickout_2",     9, 1.00f,  2000, 1200 },
    { "pinfall",       9, 1.00f,     0, 2000 },
    { "submission",    5, 0.75f, 10000,  800 },
    { "rope_break",    5, 0.80f,  8000,  800 },
    { "escape",        4, 0.60f,  8000,  600 },
    { "tap_out",       9, 1.00f,     0, 2000 },
    { "outside",       4, 0.60f, 12000,  800 },
    { "back_in",       2, 0.35f, 15000,    0 },
    { "outside_hit",   5, 0.65f,  9000,  700 },
    { "top_rope",      5, 0.75f, 10000,  600 },
    { "aerial",        6, 0.85f,  6000,  900 },
    { "aerial_miss",   6, 0.85f,  6000,  900 },
    { "weapon",        6, 0.85f, 12000,  900 },
    { "weapon_hit",    6, 0.80f,  5000,  700 },
    { "table",         8, 1.00f,  4000, 1200 },
    { "blood",         6, 0.90f, 45000, 3000 },
    { "tag",           5, 0.80f,  8000,  800 },
    { "save",          6, 0.90f,  8000,  900 },
    { "taunt",         3, 0.45f, 15000,    0 },
    { "run_in",        7, 1.00f, 20000, 2000 },
    { "dominate",      2, 0.50f, 25000, 4000 },
    { "comeback",      4, 0.70f, 25000, 4000 },
    { "tired",         2, 0.50f, 60000,    0 },
    { "elimination",   8, 1.00f,  2000, 1500 },
    { "rumble_entry",  6, 0.90f,  3000, 2500 },
    { "cage",          5, 0.70f, 12000,  800 },
    { "ladder",        5, 0.70f, 12000,  800 },
    { "win",           9, 1.00f,     0, 3000 },
    { "dq",            9, 1.00f,     0, 2000 },
    { "entrance",      6, 0.85f,  5000, 4000 },
    { "sign_off",      7, 1.00f, 60000, 6000 },
    { "filler",        1, 1.00f,  9000,    0 },
    { "danger",        4, 0.60f, 20000,    0 },
    { "name",          0, 0.00f,     0,    0 },
};
const float talk_scale[4] = { 0.35f, 0.65f, 1.0f, 1.4f };
const int filler_gap[4] = { 0, 22000, 14000, 8000 };   // ms of silence in a match before a filler line

uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

} // namespace

const char* event_name(int ev) { return ev > 0 && ev < EV_COUNT_ ? rules[ev].name : "none"; }
int event_from_name(const std::string& s) {
    for (int i = 1; i < EV_COUNT_; i++) if (s == rules[i].name) return i;
    return 0;
}

static std::vector<std::string> split_tabs(const std::string& line, int n) {
    std::vector<std::string> f;
    size_t q = 0;
    for (int k = 0; k < n - 1; k++) {
        size_t t = line.find('\t', q);
        if (t == std::string::npos) break;
        f.push_back(line.substr(q, t - q));
        q = t + 1;
    }
    f.push_back(line.substr(q));
    return f;
}

bool Director::load(const uint8_t* pak, size_t size) {
    clip.clear(); data_.clear(); move_name_.clear();
    if (size < 28 || memcmp(pak, "NMCP", 4) != 0 || (le32(pak + 4) < 2 || le32(pak + 4) > 4)) return false;
    const int ver = (int)le32(pak + 4), cols = ver >= 4 ? 11 : ver >= 3 ? 10 : 8;
    uint32_t nd = le32(pak + 8), nl = le32(pak + 12), io = le32(pak + 16), il = le32(pak + 20), to = le32(pak + 24);
    if ((uint64_t)io + il > size || (uint64_t)to + 8ull * nd > size) return false;
    for (uint32_t i = 0; i < nd; i++) {
        uint32_t off = le32(pak + to + 8 * i), len = le32(pak + to + 8 * i + 4);
        if ((uint64_t)off + len > size) { off = 0; len = 0; }
        data_.push_back({ off, len });
    }
    std::string idx((const char*)pak + io, il);
    size_t p = 0;
    bool moves = false;
    while (p < idx.size()) {
        size_t e = idx.find('\n', p);
        if (e == std::string::npos) e = idx.size();
        std::string line = idx.substr(p, e - p);
        p = e + 1;
        if (line == "#moves") { moves = true; continue; }
        if (moves) {
            std::vector<std::string> f = split_tabs(line, 2);
            if (f.size() < 2) continue;
            int m = atoi(f[0].c_str());
            if (m < 0 || m > 0xFFFF) continue;
            if ((int)move_name_.size() <= m) move_name_.resize(m + 1);
            move_name_[m] = f[1];
            continue;
        }
        std::vector<std::string> f = split_tabs(line, cols);
        if ((int)f.size() < cols) continue;
        Clip c;
        c.data = atoi(f[0].c_str());
        if (c.data < 0 || c.data >= (int)nd || !data_[c.data].second) continue;
        c.ev = event_from_name(f[1]);
        c.victim = !f[2].empty() && f[2][0] == 'v';
        c.wrestler = f[2] == "-" ? -1 : f[2].back() == '*' ? -2 : (int)strtol(f[2].c_str() + (c.victim ? 1 : 0), nullptr, 16);
        c.sex = f[3].empty() ? '-' : f[3][0];
        c.ms = (uint32_t)strtoul(f[4].c_str(), nullptr, 10);
        c.weight = (float)atof(f[5].c_str());
        c.move = f[6] == "-" ? "" : f[6];
        if (cols >= 10) { c.src = atoi(f[7].c_str()) & 7; c.speaker = f[8].empty() ? '-' : f[8][0]; }
        if (cols >= 11 && f[9] != "-") {
            size_t q = f[9].find(':');
            c.splice = true; c.heat = (float)atof(f[9].c_str()); c.db = q == std::string::npos ? 0.0f : (float)atof(f[9].c_str() + q + 1);
        }
        c.text = f[cols - 1];
        if (c.ev > 0) clip.push_back(c);
    }
    (void)nl;
    by_ev.assign(EV_COUNT_, {});
    names_.assign(0x1000, {});
    for (int i = 0; i < (int)clip.size(); i++) {
        if (clip[i].ev == EV_NAME) { if (clip[i].wrestler >= 0) names_[clip[i].wrestler & 0xFFF].push_back(i); continue; }
        by_ev[clip[i].ev].push_back(i);
    }
    last_said.assign(160, -1);
    reset();
    return !clip.empty();
}

// does a line naming `key` fit move m? (the line's word in the move's name; a few the game words otherwise)
bool Director::move_fits(const std::string& key, int move) const {
    if (!key.empty() && key[0] == '#')           // a Rumble entrant's number: move = -(number + 2)
        return move <= -2 && atoi(key.c_str() + 1) == -move - 2;
    const std::string& n = move_name(move);
    if (n.empty()) return false;
    if (n.find(key) != std::string::npos) return true;
    // a kind and a move ("german suplex"): both words in the move's name
    size_t sp = key.find(' ');
    if (sp != std::string::npos && key.find(' ', sp + 1) == std::string::npos) {
        std::string a = key.substr(0, sp), b = key.substr(sp + 1);
        if (n.find(a) != std::string::npos && (n.find(b) != std::string::npos || move_fits(b, move))) return true;
    }
    static const char* syn[][4] = {
        { "punch", "jab", "haymaker", "hook" }, { "kick", "boot", "heel", nullptr },
        { "neck breaker", "neckbreaker", nullptr, nullptr }, { "back breaker", "backbreaker", nullptr, nullptr },
        { "figure four", "figure 4", nullptr, nullptr }, { "arm bar", "armbar", nullptr, nullptr },
        { "rock bottom", "rockbottom", nullptr, nullptr }, { "splash", "senton", nullptr, nullptr },
        { "head butt", "headbutt", nullptr, nullptr }, { "clothesline", "lariat", nullptr, nullptr },
        { "body slam", "scoop slam", nullptr, nullptr }, { "superkick", "sweet chin music", nullptr, nullptr },
    };
    for (auto& s : syn) {
        if (key != s[0]) continue;
        for (int k = 1; k < 4 && s[k]; k++) if (n.find(s[k]) != std::string::npos) return true;
    }
    return false;
}

bool Director::place_fits(const std::string& key) const {
    const int venue = place_ & 0x7F;
    const bool rumble = (place_ & 0x80) != 0;
    if (key == "@rrmatch") return rumble;
    if (key == "@rumble") return rumble && venue == 7;
    static const char* venues[] = { "@raw", "@nomercy", "@smackdown", "@kotr", "@summerslam", "@survivor", "@royalrumble", "@wrestlemania" };
    for (int i = 0; i < 8; i++) if (key == venues[i]) return venue == i + 1;
    return false;
}

bool Director::line_fits(const Clip& k, int move) const {
    if (k.move.empty()) return true;
    if (k.move[0] == '@') return place_fits(k.move);
    if (k.move[0] == '+') return repeat_ && (k.move.size() == 1 || move_fits(k.move.substr(1), move));
    return move_fits(k.move, move);
}

bool Director::name_fits(int n, int lead) const {
    const Clip& L = clip[lead];
    const Clip& k = clip[n];
    return usable(n) && k.src == L.src && std::fabs(k.heat - L.heat) <= SPLICE_HEAT;
}
bool Director::has_name(int w, int lead) const {
    if (w < 0 || names_.empty()) return false;
    for (int n : names_[w & 0xFFF]) if (name_fits(n, lead)) return true;
    return false;
}
int Director::name_for(int w, int lead) {
    chain_gain_ = 1.0f;
    if (w < 0 || names_.empty()) return -1;
    // the ways of saying it (each as likely, however many takes of each), then the take nearest the lead-in's heat
    std::vector<const std::string*> ways;
    for (int n : names_[w & 0xFFF]) {
        if (!name_fits(n, lead)) continue;
        bool have = false;
        for (auto* s : ways) if (*s == clip[n].text) { have = true; break; }
        if (!have) ways.push_back(&clip[n].text);
    }
    if (ways.empty()) return -1;
    const std::string& way = *ways[rnd() % ways.size()];
    int best = -1;
    float bd = 1e9f;
    for (int n : names_[w & 0xFFF]) {
        if (!name_fits(n, lead) || clip[n].text != way) continue;
        float d = std::fabs(clip[n].heat - clip[lead].heat) + (recent(clip[n].data) ? 1.0f : 0.0f);
        if (d < bd) { bd = d; best = n; }
    }
    if (best >= 0) {
        float g = clip[lead].db - clip[best].db;
        g = g < -6.0f ? -6.0f : g > 6.0f ? 6.0f : g;
        chain_gain_ = std::pow(10.0f, g / 20.0f);
    }
    return best;
}

void Director::reset() {
    last_move_by.assign(0x1000, { -1, 0 });
    repeat_ = false;
    ev_last.assign(EV_COUNT_, 0);
    named_at.assign(0x1000, 0);
    last_any = 0;
    match_start = 0;
    pend.ev = 0;
    playing_ = -1;
    until_ = 0;
    follow_.ev = 0;
}

bool Director::recent(int c) const {
    for (int x : last_said) if (x == c) return true;
    return false;
}

// A line for the event: the wrestler's own (by name) first when asked and he has some he wasn't just
// named in, else the general ones; never one of the last 160 said; his/her lines by sex.
int Director::pick(int ev, int a, int b, bool female, uint64_t now_ms, bool named_first, int move) {
    chain_ = -1;
    const std::vector<int>& L = by_ev[ev];
    if (L.empty()) return -1;
    char sex = female ? 'F' : 'M';
    // the lines that name this move, if any fit, most of the time
    bool by_move = false;
    // (where the match is: its own hello and good night most of the time, now and then its own talk)
    const bool by_place = place_ && ((ev == EV_MATCH_START || ev == EV_SIGN_OFF) ? frand() < 0.85f :
                                     ev == EV_FILLER ? frand() < 0.3f : false);
    if ((move >= 0 && frand() < 0.75f) || move <= -2 || (repeat_ && frand() < 0.7f) || by_place)
        for (int c : L) if (usable(c) && !clip[c].move.empty() && !recent(clip[c].data) && line_fits(clip[c], move)) { by_move = true; break; }
    auto fresh_name = [&](int w) { return w >= 0 && !(named_at[w & 0xFFF] && now_ms - named_at[w & 0xFFF] < 15000); };
    // now and then a spliced call: one of JR's lead-ins and a name of the one it's about ("What a move by" ...
    // "Kurt Angle") -- as often whoever he is (not by how many lines of his own he has)
    if (splice_on_ && want_spk_ != 'K' && frand() < SPLICE_CHANCE) {
        int pickc = -1, n = 0;
        for (int c : L) {
            const Clip& k = clip[c];
            if (k.wrestler != -2 || !usable(c) || recent(k.data) || !line_fits(k, move)) continue;
            const int who = k.victim ? b : a;
            if (!fresh_name(who) || !has_name(who, c)) continue;
            if (frand() * ++n < 1.0f) pickc = c;
        }
        if (pickc >= 0) {
            const int who = clip[pickc].victim ? b : a;
            chain_ = name_for(who, pickc);
            if (chain_ >= 0) { ctx_ = { ev, a, b, move, female, repeat_ }; return pickc; }
        }
    }
    for (int pass = 0; pass < 2; pass++) {
        bool named = (pass == 0) == named_first;
        if (named && !fresh_name(a) && !fresh_name(b)) continue;
        float total = 0;
        int cand = 0;
        for (int round = 0; round < 2; round++) {
            float r = round ? frand() * total : 0;
            for (int c : L) {
                const Clip& k = clip[c];
                // (a lead-in: one of the named lines, when the one it's about has a name to follow it)
                const int who = k.victim ? b : a;
                if (k.wrestler == -2) continue;          // (lead-ins: only as a spliced call, above)
                if (named ? (k.wrestler < 0 || k.wrestler != who || !fresh_name(k.wrestler)) : k.wrestler != -1) continue;
                if (k.sex != '-' && k.sex != sex) continue;
                if (!usable(c)) continue;
                if (want_spk_ && toupper((unsigned char)k.speaker) != want_spk_) continue;
                if (recent(k.data)) continue;
                if (!k.move.empty() ? !line_fits(k, move) : by_move) continue;
                if (round == 0) { total += k.weight; cand++; continue; }
                r -= k.weight;
                if (r <= 0) {
                    ctx_ = { ev, a, b, move, female, repeat_ };
                    return c;
                }
            }
            if (round == 0 && (total <= 0 || !cand)) break;
        }
    }
    // all said lately: the oldest-said general one rather than nothing, for the must-say moments
    if (rules[ev].prio >= 8 && !want_spk_)
        for (int c : L) if (usable(c) && clip[c].wrestler == -1 && clip[c].move.empty() && (clip[c].sex == '-' || clip[c].sex == sex)) return c;
    return -1;
}

int Director::event(int ev, int a, int b, int arg, bool female, uint64_t now_ms, int move) {
    if (!on_ || ev <= 0 || ev >= EV_COUNT_ || clip.empty()) return -1;
    const Rule& r = rules[ev];
    if (ev == EV_MATCH_START) match_start = now_ms;
    if ((ev == EV_MATCH_START || ev == EV_SIGN_OFF) && (arg & 0xFF)) place_ = arg & 0xFF;
    // the same move again by the same man (within half a minute): the "another one" lines
    if (replaying_) repeat_ = pend.repeat;
    else repeat_ = false;
    if (!replaying_ && a >= 0 && move >= 0 && (ev == EV_STRIKE || ev == EV_STRIKE_BIG || ev == EV_GRAPPLE || ev == EV_BIG_MOVE ||
                                ev == EV_SUBMISSION || ev == EV_AERIAL || ev == EV_FINISHER)) {
        auto& lm = last_move_by[a & 0xFFF];
        repeat_ = lm.first == move && now_ms - lm.second < 30000;
        lm = { move, now_ms };
    }
    bool busy = playing_ >= 0 && now_ms < until_;
    // a bigger moment cuts in (3 above what's on); the live calls (the count, kick-outs, the finish) cut
    // in on anything not above them
    bool cuts = r.prio >= playing_prio_ + 3 || (r.prio >= 8 && r.prio >= playing_prio_);
    if (busy && !cuts) {
        // it may wait a moment for the line on now (fresh): kept, the highest such, for tick()
        if (r.fresh && (!pend.ev || now_ms > pend.until || rules[pend.ev].prio <= r.prio)) {
            pend.ev = ev; pend.a = a; pend.b = b; pend.arg = arg; pend.female = female; pend.move = move; pend.repeat = repeat_;
            pend.until = until_ + (uint64_t)r.fresh;
            if (now_ms + r.fresh > pend.until) pend.until = now_ms + r.fresh;
        }
        return -1;
    }
    if (r.cooldown && ev_last[ev] && now_ms - ev_last[ev] < (uint64_t)r.cooldown) return -1;
    float ch = r.chance >= 1.0f ? 1.0f : r.chance * talk_scale[talk_ & 3];
    if (frand() >= ch) return -1;
    // the referee's count: JR's "One!" "Two!" lines are the count event's, by its number
    int c;
    if (ev == EV_COUNT) {
        c = -1;
        const std::vector<int>& L = by_ev[ev];
        int k = 0;
        for (int x : L) {
            // the index text starts with the number said ("One", "Two", "Three"/"That's three")
            const std::string& t = clip[x].text;
            std::string l = t;
            for (char& ch : l) ch = (char)tolower((unsigned char)ch);
            int n = (l.find("three") != std::string::npos || l.find('3') != std::string::npos) ? 3 :
                    (l.find("two") != std::string::npos || l.find('2') != std::string::npos) ? 2 : 1;
            if (n == arg && usable(x) && !recent(clip[x].data) && (frand() * ++k) < 1.0f) c = x;
        }
        ctx_ = { ev, a, b, -1, female, false };
    } else {
        c = pick(ev, a, b, female, now_ms, frand() < 0.6f, ev == EV_RUMBLE_ENTRY ? -(arg + 2) : move);
    }
    if (c < 0) return -1;
    ev_last[ev] = now_ms;
    return c;
}

int Director::tick(uint64_t now_ms, bool in_match) {
    if (!on_ || clip.empty()) return -1;
    if (!in_match && !pend.ev && !follow_.ev) return -1;
    if (playing_ >= 0 && now_ms < until_) return -1;
    if (pend.ev) {
        int ev = pend.ev; pend.ev = 0;
        follow_.ev = 0;                     // (the moment has moved on)
        if (now_ms <= pend.until) {
            replaying_ = true;
            int c = event(ev, pend.a, pend.b, pend.arg, pend.female, now_ms, pend.move);
            replaying_ = false;
            if (c >= 0) return c;
        }
    }
    // the other man on the line just said: right after it, or not at all
    if (follow_.ev) {
        auto f = follow_;
        follow_.ev = 0;
        if (now_ms >= f.at && now_ms - f.at < 1500) {
            // said to him: he answers most of the time; else King now and then has his say on JR's call
            float ch = f.to ? 0.75f : (f.from == 'J' ? 0.30f * talk_scale[talk_ & 3] : 0.0f);
            if (frand() < ch) {
                want_spk_ = f.from == 'J' ? 'K' : 'J';
                bool rep = repeat_;
                repeat_ = f.repeat;
                int c = pick(f.ev, f.a, f.b, f.female, now_ms, frand() < 0.5f, f.ev == EV_RUMBLE_ENTRY ? -1 : f.move);
                // (said to him, and nothing of his for that moment: something of his for any quiet moment)
                if (c < 0 && f.to && in_match) c = pick(EV_FILLER, -1, -1, false, now_ms, false, -1);
                repeat_ = rep;
                want_spk_ = 0;
                if (c >= 0) { following_ = true; follow_.depth = f.depth + 1; return c; }
            }
        }
    }
    uint64_t quiet = last_any ? now_ms - last_any : (match_start ? now_ms - match_start : 0);
    if (!in_match || talk_ == 0 || quiet < (uint64_t)filler_gap[talk_ & 3]) return -1;
    int c = pick(EV_FILLER, -1, -1, false, now_ms, false, -1);
    if (c >= 0) ev_last[EV_FILLER] = now_ms;
    return c;
}

// the moments a line from the other man can follow (not the count, a cover, a quick kick-out)
static bool answerable(int ev) {
    switch (ev) {
    case EV_COUNT: case EV_PIN: case EV_KICKOUT_1: case EV_ROPE_BREAK: case EV_NONE: return false;
    default: return true;
    }
}

void Director::started(int c, uint64_t now_ms) {
    if (c < 0 || c >= (int)clip.size()) return;
    // the booth: who said it, what about, and when the other man could come in
    const Clip& k = clip[c];
    int depth = following_ ? follow_.depth : 0;
    following_ = false;
    follow_.ev = 0;
    char who = (char)toupper((unsigned char)k.speaker);
    bool to = k.speaker == 'j' || k.speaker == 'k';
    if ((who == 'J' || who == 'K') && answerable(ctx_.ev) && ctx_.ev == k.ev && (depth == 0 || (to && depth < 2))) {
        follow_.ev = ctx_.ev; follow_.a = ctx_.a; follow_.b = ctx_.b; follow_.move = ctx_.move;
        follow_.female = ctx_.female; follow_.repeat = ctx_.repeat; follow_.from = who; follow_.to = to;
        follow_.depth = depth; follow_.at = now_ms + k.ms + 250;
    }
    playing_ = c;
    playing_prio_ = rules[clip[c].ev].prio;
    until_ = now_ms + clip[c].ms + 250;      // (a breath after it)
    if (k.wrestler == -2 && chain_ >= 0) {     // (the name after a lead-in)
        until_ += clip[chain_].ms + CHAIN_GAP_MS;
        if (playing_prio_ < 7) playing_prio_ = 7;   // (a spliced call isn't cut off for less than the live calls)
        last_said[last_pos++ % last_said.size()] = clip[chain_].data;
        if (clip[chain_].wrestler >= 0) named_at[clip[chain_].wrestler & 0xFFF] = now_ms;
        follow_.at = until_;
    }
    last_any = until_;
    last_said[last_pos++ % last_said.size()] = clip[c].data;
    if (clip[c].wrestler >= 0) named_at[clip[c].wrestler & 0xFFF] = now_ms;
}

} // namespace comm
