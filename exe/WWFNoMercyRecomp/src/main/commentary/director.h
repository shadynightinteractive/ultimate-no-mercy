// [arcade] Match commentary (Jim Ross & Jerry Lawler, from WWE '13's Attitude Era lines): the director.
// Platform-free: the exe (arcade_commentary.cpp) feeds it the mod's match events and plays what it
// picks; the test harness (commentary/sim.cpp) does the same from an event log.
//
// The pack (commentary.pak, made by gen/mkcommentary.py):
//   "NMCP" u32 version (2, 3 or 4), u32 clips, u32 lines, u32 index offset, u32 index length, u32 table offset
//   table: per clip u32 offset, u32 length (an Ogg Vorbis file, mono)
//   index: text, one line per line said (a clip can be several: one per event / wrestler), tab-separated:
//          clip, event, wrestler (No Mercy id, hex; - none; * a lead-in, his name follows -- v* the victim's), sex (M/F/-), milliseconds, weight,
//          move (the move it names, - none; "+move" / "+": a line for doing it again: "Another suplex!";
//          "@place": a line for where the match is -- @rumble (a Royal Rumble in its arena), @rrmatch (a Royal
//          Rumble anywhere), @raw, @smackdown, @kotr, @summerslam, @survivor, @wrestlemania: the arena), [version 3: source game (0 WWE '13, 1 Legends), speaker
//          (J / K / -),] [version 4: splice -- a lead-in's / name's "heat:dB" (how worked up he is at the join, its
//          level), - for the rest,] text -- [commentary2 2026-11-18] or, in packs made from the shipped recipe, a TAG instead
//          of the text: "way:N" (a name said the same way), "count:N" (the number a count line says), - (the rest)
// all little-endian. The pack may be in parts (commentary.pak, .pak.1, ...): read as one.
#pragma once
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace comm {

constexpr int CHAIN_GAP_MS = 40;      // between a lead-in and the name after it
constexpr int CHAIN_FADE_MS = 20;     // the lead-in's last moment eased out (he runs "by" into the name)
constexpr float SPLICE_CHANCE = 0.25f;   // how often a moment that could get a spliced call gets one (else the usual lines)
constexpr float SPLICE_HEAT = 0.5f;      // a name joins a lead-in only if he's about as worked up in both

// What the mod reports (recomp_arcade_comm_event's first argument). Keep in step with the mod's
// src/commentary.c and gen/mkcommentary.py's CATEGORIES.
enum Event : int {
    EV_NONE = 0,
    EV_MATCH_START,     // the bell
    EV_LOCKUP,          // a collar-and-elbow tie-up
    EV_STRIKE,          // a punch / kick that lands
    EV_STRIKE_BIG,      // a running or strong strike that lands
    EV_GRAPPLE,         // a weak grapple move
    EV_BIG_MOVE,        // a strong grapple move (slam, suplex, driver)
    EV_KNOCKDOWN,       // put down on the mat
    EV_REVERSAL,        // a counter
    EV_SPECIAL,         // SPECIAL! lit up
    EV_FINISHER,        // a special move hits
    EV_PIN,             // a cover
    EV_COUNT,           // the referee's count: arg 1, 2, 3
    EV_KICKOUT_1,       // kicked out at one
    EV_KICKOUT_2,       // kicked out at two (a near fall)
    EV_PINFALL,         // the three count
    EV_SUBMISSION,      // a submission hold applied
    EV_ROPE_BREAK,      // out of a hold by the ropes
    EV_ESCAPE,          // out of a hold / pin some other way
    EV_TAP_OUT,         // submits
    EV_OUTSIDE,         // goes out to the floor
    EV_BACK_IN,         // back in the ring
    EV_OUTSIDE_HIT,     // a move on the floor / into the steps / barricade
    EV_TOP_ROPE,        // climbing the turnbuckle
    EV_AERIAL,          // a move off the top that hits
    EV_AERIAL_MISS,     // ... that misses
    EV_WEAPON,          // a weapon picked up
    EV_WEAPON_HIT,      // a weapon shot
    EV_TABLE,           // through a table
    EV_BLOOD,           // busted open
    EV_TAG,             // a tag
    EV_SAVE,            // a partner breaks up a pin / hold
    EV_TAUNT,           // showboating
    EV_RUN_IN,          // someone comes down to interfere
    EV_DOMINATE,        // one man well on top (the director's filler, from the mod's damage numbers)
    EV_COMEBACK,        // the one who was down fights back
    EV_TIRED,           // both slow, a long match
    EV_ELIMINATION,     // out of a Rumble / over the top
    EV_RUMBLE_ENTRY,    // a Rumble entrant: arg = number
    EV_CAGE,            // a cage / cell spot
    EV_LADDER,          // a ladder spot
    EV_WIN,             // the match is over: a = the winner
    EV_DQ,              // disqualification / count out
    EV_ENTRANCE,        // walking out: a = the wrestler
    EV_SIGN_OFF,        // after the match, leaving
    EV_FILLER,          // nothing happening: the director's own
    EV_DANGER,          // a is in trouble (his spirit in the blue / taking a beating)
    EV_NAME,            // (not an event: a Superstar's name, said on its own -- the end of a spliced line)
    EV_COUNT_
};
const char* event_name(int ev);
int event_from_name(const std::string& s);

struct Clip {
    int data = -1;            // which clip of the pack (its sound)
    int ev = 0;
    int wrestler = -1;        // a line about this wrestler (by name); -1 none; -2 a lead-in ("What a kick by...")
                              // the name of the one it's about follows (his EV_NAME clip)
    bool victim = false;      // ... as the one a move was done to (the event's b)
    char sex = '-';           // 'M' / 'F': says he / she
    uint32_t ms = 0;
    float weight = 1.0f;
    std::string move;         // the move it names ("" none)
    int src = 0;              // where it's from: 0 WWE '13, 1 Legends of WrestleMania, 2 JR's trained voice (AI)
    char speaker = '-';       // 'J' Jim Ross, 'K' Jerry Lawler, '-' not known; lower case ('j' / 'k'): said to the
                              // other man ("Look at this slam, JR!")
    bool splice = false;      // a lead-in or a name made to be joined (pack v4): heat and db at the join
    float heat = 0.0f, db = 0.0f;
    std::string text;         // what is said, or the recipe's tag (way:N / count:N / -): only these two uses read it
};

struct Said { int clip; uint64_t at; };

class Director {
public:
    bool load(const uint8_t* pak, size_t size);
    size_t clips() const { return clip.size(); }
    const Clip& at(int i) const { return clip[i]; }
    std::pair<uint32_t, uint32_t> sound(int i) const { return data_[clip[i].data]; }   // (offset, length)
    const std::string& move_name(int m) const { static std::string none; return m >= 0 && m < (int)move_name_.size() ? move_name_[m] : none; }

    // on: commentary at all; talk: 0 little .. 3 a lot (how often an ordinary moment gets a line);
    // sources: whose lines (bit 0 WWE '13, bit 1 Legends of WrestleMania; bit 2 the AI voice -- dropped 2026-11-19, never asked for)
    // splice: the spliced calls ("What a move by" + "Kurt Angle"), now and then
    void config(bool on, int talk, unsigned sources = 1, bool splice = true) { on_ = on; talk_ = talk; src_mask_ = (sources & 3) ? sources : (sources | 1); splice_on_ = splice; }

    // A match event at game time now_ms (milliseconds, steady). a / b: No Mercy wrestler ids (-1 none),
    // female: a is a woman (her lines say she). Returns the clip to start now, or -1.
    // playing_until: when the current clip ends (0: nothing playing).
    int event(int ev, int a, int b, int arg, bool female, uint64_t now_ms, int move = -1);  // move: the game's move number / 10
    // Called every frame or so: silence long enough in a match -> a filler line (or -1)
    int tick(uint64_t now_ms, bool in_match);
    // the clip that started (the player calls it when it starts one the director picked)
    void started(int c, uint64_t now_ms);
    void stopped() { playing_ = -1; until_ = 0; }
    void reset();                       // a new match: forget who was named, timers
    int playing() const { return playing_; }
    // who says the line on now: 'J' Jim Ross, 'K' Jerry Lawler, '-' not known (or none)
    char speaker() const { return playing_ >= 0 && playing_ < (int)clip.size() ? (char)toupper((unsigned char)clip[playing_].speaker) : '-'; }
    // the name to play right after the clip just picked (a lead-in's), or -1
    int chain() const { return chain_; }
    float chain_gain() const { return chain_gain_; }     // the name's level, to sit with the lead-in's
    uint64_t until() const { return until_; }
    void seed(uint32_t s) { rng_ = s ? s : 1; }

private:
    uint32_t rnd() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return rng_; }
    float frand() { return (rnd() & 0xFFFFFF) / float(0x1000000); }
    int pick(int ev, int a, int b, bool female, uint64_t now_ms, bool named_first, int move);
    bool move_fits(const std::string& key, int move) const;
    bool line_fits(const Clip& k, int move) const;     // its move (if it names one) fits; a "+" line: only on a repeat
    bool recent(int c) const;
    bool usable(int c) const { return (src_mask_ >> clip[c].src) & 1; }

    std::vector<Clip> clip;
    std::vector<std::pair<uint32_t, uint32_t>> data_;   // each clip's sound in the pack
    std::vector<std::string> move_name_;               // the game's move names (lower case), by number / 10
    std::vector<std::vector<int>> by_ev;
    std::vector<int> last_said;         // ring of recent clips (no repeats)
    size_t last_pos = 0;
    std::vector<uint64_t> ev_last;      // when each event last got a line
    uint64_t last_any = 0, match_start = 0;
    int playing_ = -1, playing_prio_ = 0;
    uint64_t until_ = 0;
    bool on_ = true;
    int talk_ = 2;
    unsigned src_mask_ = 1;
    uint32_t rng_ = 0x1234567;
    std::vector<uint64_t> named_at;     // when a wrestler was last named (by id & 0xFFF)
    std::vector<std::pair<int, uint64_t>> last_move_by;   // each wrestler's last move (by id & 0xFFF) and when
    bool repeat_ = false;               // the move now is the same one he did just before
    int place_ = 0;                     // where the match is (the bell's arg): the venue + 1 (1 Raw is War .. 8
                                        // WrestleMania), | 0x80 a Royal Rumble
    bool place_fits(const std::string& key) const;
    // the booth: after one man's line the other may come in on the same moment (King reacting to JR's call;
    // anything said to the other man most likely gets an answer)
    struct { int ev = 0, a = -1, b = -1, move = -1; bool female = false, repeat = false; } ctx_;   // the line picked last: its moment
    struct { int ev = 0, a = -1, b = -1, move = -1; bool female = false, repeat = false; char from = 0; bool to = false;
             int depth = 0; uint64_t at = 0; } follow_;
    char want_spk_ = 0;                 // pick(): only this man's lines (0: anyone's)
    bool following_ = false;            // the line picked is an answer
    int chain_ = -1;                    // the name clip to follow the clip picked (lead-ins)
    std::vector<std::vector<int>> names_;   // each wrestler's name clips (by id & 0xFFF)
    // a name of his for lead-in `lead`: the same game's, about as worked up; each way of saying it as likely
    int name_for(int w, int lead);
    bool has_name(int w, int lead) const;
    bool name_fits(int n, int lead) const;
    bool splice_on_ = true;
    float chain_gain_ = 1.0f;
    struct { int ev = 0, a = -1, b = -1, arg = 0; bool female = false; uint64_t until = 0; int move = -1; bool repeat = false; } pend;
    bool replaying_ = false;            // event() called again for the pending one (its repeat_ as it was)  // waiting for the line on now
};

} // namespace comm
