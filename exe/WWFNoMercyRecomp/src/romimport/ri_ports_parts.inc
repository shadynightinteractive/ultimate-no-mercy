// ri_ports_parts.inc (phase3 p3ports, 2026-11-19): inside Ports::Impl (ri_ports.cpp) -- gen/mkparts.py (parts_ext.h, parts.pkl's
// values) and gen/mkovr.py (parts_ovr.h).

    // ==================================================================================================================
    // gen/mkparts.py: parts_ext.h -- the new Superstars' body parts as more entries of the game's part tables, and their
    // costumes (packed keys, bit 183 set). Its own process in the chain: No Mercy at its Select fresh (van_img: the image as
    // mkroster's module made it), No Mercy Plus's image read as it is (nm_img).
    // ==================================================================================================================
    std::unique_ptr<Emu> EVp;                           // (mkparts' EV: the fresh image, its own process)
    std::set<uint32_t> used;                            // (the No Mercy Plus files its parts name)
    std::vector<std::pair<int, int>> nm_params;         // (No Mercy Plus colour param -> ours, in the order seen)
    std::map<std::string, std::map<int, int>> NEW;      // (our index -> No Mercy Plus's)
    struct Tab { uint32_t va, na; int sz, cnt; };
    static const std::map<std::string, Tab>& T() {
        static const std::map<std::string, Tab> t = {
            { "lower_kind", { 0x8004CF98, 0x8040C220, 1, 0x80 } }, { "lower_desc", { 0x8004D018, 0x8040BE00, 4, 0x80 } },
            { "lower_fem", { 0x8004FFAA, 0x804098F0, 1, 0x80 } },
            { "upper_kind", { 0x8004EB98, 0x8040ECA0, 1, 0x7D } }, { "upper_desc", { 0x8004EC18, 0x8040E850, 4, 0x7D } },
            { "face_tex", { 0x8004D254, 0x8004BF00, 2, 0x80 } }, { "face_b1", { 0x8004D354, 0x8004C110, 1, 0x76 } },
            { "face_b2", { 0x8004D3CC, 0x8004C220, 1, 0x76 } }, { "face_tv", { 0x8004FE8C, 0x8040F700, 2, 0x80 } },
            { "fh_tex", { 0x8004D528, 0x8004C340, 2, 0x20 } }, { "fh_b", { 0x8004D568, 0x8004C3E0, 1, 0x20 } },
            { "mask_desc", { 0x8004EF84, 0x8004E2C0, 4, 0x20 } }, { "mask_b", { 0x8004F004, 0x8004E3E0, 1, 0x20 } },
            { "param_kind", { 0x80049804, 0x8040CD00, 1, 0x48 } }, { "param_files", { 0x800496E4, 0x8040C820, 4, 0x48 } },
        };
        return t;
    }
    static constexpr uint32_t NM_BLOCK = 0x5290, NM_BLOCK_END = 0x52E0, OUR_BLOCK = 0x4C60;   // (No Mercy Plus's new files)
    static constexpr uint32_t NM_FACES = 0x1E16, NM_FACES_END = 0x1E1A, OUR_FACES = 0x4CB0;   // (the faces it drew over the game's)
    static constexpr uint32_t NM_JJ = 0x5128, NM_JJ_END = 0x5134, OUR_JJ = 0x4CB4;             // (Jeff Jarrett's attire and face)
    static constexpr int LOWER_N = 0x84, UPPER_EXT0 = 0x80, FACE_EXT0 = 0x80, FACE_EXT_N = 3, FH_N = 0x22, MASK_N = 0x22,
                         PARAM_EXT0 = 0x80, PARAM_EXT_N = 1;
    Bytes vr(uint32_t a, size_t n) const { a &= 0x7FFFFF; return Bytes(van_img.begin() + a, van_img.begin() + a + n); }
    Bytes nr(uint32_t a, size_t n) const { a &= 0x7FFFFF; return Bytes(nm_img.begin() + a, nm_img.begin() + a + n); }
    static uint32_t ent(const Bytes& b, int sz, size_t i) { return sz == 1 ? b[i] : sz == 2 ? be16(b, 2 * i) : be32(b, 4 * i); }
    uint32_t nm_entry(const std::string& name, int i) const {
        if (!NP) return 0;                       // (nmplus: No Mercy Plus isn't there -- its new entries zero)
        const Tab& t = T().at(name); return ent(nr(t.na + t.sz * i, t.sz), t.sz, 0);
    }
    std::vector<uint32_t> van_entries(const std::string& name) const {
        const Tab& t = T().at(name);
        Bytes b = vr(t.va, (size_t)t.sz * t.cnt);
        std::vector<uint32_t> o; for (int i = 0; i < t.cnt; i++) o.push_back(ent(b, t.sz, i));
        return o;
    }
    static bool nm_new(uint32_t f) { return (NM_BLOCK <= f && f < NM_BLOCK_END) || (NM_FACES <= f && f < NM_FACES_END) || (NM_JJ <= f && f < NM_JJ_END); }
    uint32_t our_file(uint32_t f) {
        if (NM_BLOCK <= f && f < NM_BLOCK_END) return OUR_BLOCK + (f - NM_BLOCK);
        if (NM_FACES <= f && f < NM_FACES_END) return OUR_FACES + (f - NM_FACES);
        if (NM_JJ <= f && f < NM_JJ_END) return OUR_JJ + (f - NM_JJ);
        const Bytes *a = NP->get(f), *b = N->get(f);
        if (!(f < 0x4C03 && ((!a && !b) || (a && b && *a == *b)))) fail("ports: file %04x: No Mercy Plus's differs from the game's", f);
        return f;
    }
    uint32_t ref(uint32_t f, std::initializer_list<int> companions = { -1, 0, 1, 2, 3 }) {
        if (!NP) return 0;                       // (nmplus: none of No Mercy Plus's files)
        uint32_t g = our_file(f);
        if (g != f) for (int d : companions) if (NP->get(f + d) && nm_new(f + d)) used.insert(f + d);
        return g;
    }
    int param(int p) {
        if (p == 0 || p < 0x48) return p;
        for (auto& [a, b] : nm_params) if (a == p) return b;
        if ((int)nm_params.size() >= PARAM_EXT_N) fail("ports: more colour params than PARAM_EXT_N");
        int ours = PARAM_EXT0 + (int)nm_params.size();
        nm_params.push_back({ p, ours });
        NEW["param"][ours] = p;
        return ours;
    }
    Bytes lower_desc(int nmi) {
        Bytes d = nr(nm_entry("lower_desc", nmi), 0x24);
        for (size_t i = 0; i < 0x22; i += 2) { uint32_t f = be16(d, i); if (f) put_be16(&d[i], ref(f)); }
        d[0x23] = (uint8_t)param(d[0x23]);
        return d;
    }
    Bytes upper_desc(int nmi) {
        Bytes s = nr(nm_entry("upper_desc", nmi), 0x1A);
        if (be16(s, 0x16) != 0 || s[0x18] != 0) fail("ports: No Mercy Plus's upper body %d uses its 12th file", nmi);
        Bytes d(0x18);
        for (size_t i = 0; i < 0x16; i += 2) { uint32_t f = be16(s, i); if (f) put_be16(&d[i], ref(f)); }
        d[0x17] = (uint8_t)param(s[0x19]);
        return d;
    }
    Bytes mask_desc(int nmi) {
        Bytes s = nr(nm_entry("mask_desc", nmi), 8), d(8);
        for (size_t i = 0; i < 8; i += 2) { uint32_t f = be16(s, i); if (f) put_be16(&d[i], ref(f)); }
        return d;
    }
    // a table's values: a number, or the name of a descriptor (pd_<name>)
    struct Val { uint32_t v = 0; std::string pd; };
    std::vector<std::pair<std::string, std::vector<Val>>> tabs;   // (in the Python dict's order)
    std::map<std::string, Bytes> descs;                           // (sorted by name, as the output)
    static std::vector<Val> nums(const std::vector<uint32_t>& v) { std::vector<Val> o; for (auto x : v) o.push_back({ x }); return o; }
    void build_tables() {
        tabs.clear(); descs.clear(); used.clear(); nm_params.clear();
        NEW = { { "lower", { { 128, 128 }, { 129, 131 }, { 130, 143 }, { 131, 151 } } }, { "upper", { { 0x80, 107 } } },
                { "face", { { 0x80, 82 }, { 0x81, 83 }, { 0x82, 132 } } }, { "fh", { { 32, 32 }, { 33, 33 } } },
                { "mask", { { 32, 32 }, { 33, 33 } } }, { "param", {} } };
        auto add = [&](const std::string& n, std::vector<Val> v) { tabs.push_back({ n, v }); };
        std::vector<int> LOW, FAC; for (auto& [k, v] : NEW["lower"]) LOW.push_back(k); for (auto& [k, v] : NEW["face"]) FAC.push_back(k);
        { auto v = nums(van_entries("lower_kind")); for (int i : LOW) v.push_back({ nm_entry("lower_kind", NEW["lower"][i]) }); add("lower_kind", v); }
        { auto v = nums(van_entries("lower_fem")); for (int i : LOW) v.push_back({ nm_entry("lower_fem", NEW["lower"][i]) }); add("lower_fem", v); }
        { auto v = nums(van_entries("lower_desc")); for (int i : LOW) v.push_back({ 0, strf("lower_%d", i) }); add("lower_desc", v); }
        if (NP) for (int i : LOW) descs[strf("lower_%d", i)] = lower_desc(NEW["lower"][i]);   // (nmplus: without it, none -- zero)
        // (the game's are 0x7D long, and 0x7D-0x7F is "none": a packed upper body can be those)
        { auto v = nums(van_entries("upper_kind")); for (int k = 0; k < 3; k++) v.push_back({ 0 }); v.push_back({ nm_entry("upper_kind", NEW["upper"][0x80]) }); add("upper_kind", v); }
        { auto v = nums(van_entries("upper_desc")); for (int k = 0; k < 3; k++) v.push_back({ 0 }); v.push_back({ 0, "upper_128" }); add("upper_desc", v); }
        if (NP) descs["upper_128"] = upper_desc(NEW["upper"][0x80]);
        { auto v = nums(van_entries("face_tex")); for (int i : FAC) v.push_back({ ref(nm_entry("face_tex", NEW["face"][i]), { 0, 1 }) }); add("face_tex", v); }
        for (const char* n : { "face_b1", "face_b2" }) {     // (the game's are 0x76 long; 0x76-0x7F aren't read)
            auto v = nums(van_entries(n)); for (int k = 0x76; k < 0x80; k++) v.push_back({ 0 });
            for (int i : FAC) v.push_back({ nm_entry(n, NEW["face"][i]) });
            add(n, v);
        }
        { auto v = nums(van_entries("face_tv")); for (int i : FAC) v.push_back({ nm_entry("face_tv", NEW["face"][i]) }); add("face_tv", v); }
        { auto v = nums(van_entries("fh_tex")); for (int i : { 32, 33 }) v.push_back({ ref(nm_entry("fh_tex", NEW["fh"][i]), { 0, 1, 2, 3 }) }); add("fh_tex", v); }
        { auto v = nums(van_entries("fh_b")); for (int i : { 32, 33 }) v.push_back({ nm_entry("fh_b", NEW["fh"][i]) }); add("fh_b", v); }
        { auto v = nums(van_entries("mask_desc")); for (int i : { 32, 33 }) v.push_back({ 0, strf("mask_%d", i) }); add("mask_desc", v); }
        if (NP) for (int i : { 32, 33 }) descs[strf("mask_%d", i)] = mask_desc(NEW["mask"][i]);
        { auto v = nums(van_entries("mask_b")); for (int i : { 32, 33 }) v.push_back({ nm_entry("mask_b", NEW["mask"][i]) }); add("mask_b", v); }
        // the new colour params (named by the descriptors above): the kind's own lists must be the game's
        static const uint32_t VAN_PK[3] = { 0x80049598, 0x8004955C, 0x8004958C }, NM_PK[3] = { 0x80049638, 0x800495FC, 0x8004962C };
        auto pk = nums(van_entries("param_kind")), pf = nums(van_entries("param_files"));
        for (int k = 0x48; k < PARAM_EXT0; k++) { pk.push_back({ 0 }); pf.push_back({ 0 }); }
        for (int ours = PARAM_EXT0; ours < PARAM_EXT0 + PARAM_EXT_N; ours++) {
            auto it = NEW["param"].find(ours);
            if (it == NEW["param"].end()) { pk.push_back({ 0 }); pf.push_back({ 0 }); continue; }
            int p = it->second;
            uint32_t k = nm_entry("param_kind", p); pk.push_back({ k });
            int cnt = sx8(nr(NM_PK[2] + k, 1)[0]);
            if (cnt != sx8(vr(VAN_PK[2] + k, 1)[0])) fail("ports: param kind %u's count", k);
            if (vr(be32(vr(VAN_PK[0] + 4 * k, 4), 0), 2 * cnt) != nr(be32(nr(NM_PK[0] + 4 * k, 4), 0), 2 * cnt)) fail("ports: param kind %u's list", k);
            for (int typ = 0; typ < 12; typ++) {
                uint32_t vp = be32(vr(be32(vr(VAN_PK[1] + 4 * k, 4), 0) + 4 * typ, 4), 0), np_ = be32(nr(be32(nr(NM_PK[1] + 4 * k, 4), 0) + 4 * typ, 4), 0);
                if (!(0x80000000u <= vp && vp < 0x80800000u && 0x80000000u <= np_ && np_ < 0x80800000u)) continue;
                if (vr(vp, 2 * cnt) != nr(np_, 2 * cnt)) fail("ports: param kind %u type %d", k, typ);
            }
            Bytes lst = nr(nm_entry("param_files", p), 2 * cnt), d;
            for (int i = 0; i < cnt; i++) { uint32_t f = be16(lst, 2 * i), g = f ? ref(f, { 0, 1 }) : 0; d.push_back((uint8_t)(g >> 8)); d.push_back((uint8_t)g); }
            descs[strf("param_%d", ours)] = d;
            pf.push_back({ 0, strf("param_%d", ours) });
        }
        add("param_kind", pk); add("param_files", pf);
    }

    // ---- the costumes ----------------------------------------------------------------------------------------------------
    static constexpr uint32_t REC = 0x80780000, UNB = 0x80780400;
    Bytes van_unpack(const Bytes& packed) {
        Emu& E = *EVp;
        E.wbytes(UNB, Bytes(0x40)); E.wbytes(REC, packed); E.call(0x8001AC44, { REC, UNB }); return E.rbytes(UNB, 0x3F);
    }
    Bytes van_pack(const Bytes& un) {
        Emu& E = *EVp;
        Bytes u = un; u.push_back(0);
        E.wbytes(UNB, u); E.wbytes(REC, Bytes(0x18)); E.call(0x8001A9A0, { UNB, REC }); return E.rbytes(REC, 0x17);
    }
    struct Costume { Bytes nm_un, un, key; };
    Costume costume_src(const Bytes& src) {
        // the appearance table's 0x26 bytes -> the unpacked struct (func_8000B60C)
        static const std::pair<int, int> SRC2UN[] = { { 0, 0 }, { 1, 1 }, { 2, 3 }, { 3, 4 }, { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 8 }, { 8, 9 },
            { 9, 0xA }, { 0xA, 0xB }, { 0x1C, 0xC }, { 0x1D, 0xF }, { 0x1E, 0x12 }, { 0x1F, 0x13 }, { 0x20, 0x15 }, { 0x21, 0x18 },
            { 0x22, 0x1B }, { 0xC, 0x1E }, { 0xD, 0x1F }, { 0x11, 0x21 }, { 0x12, 0x22 }, { 0x13, 0x24 }, { 0x14, 0x25 }, { 0xF, 0x27 },
            { 0x10, 0x28 }, { 0x15, 0x2A }, { 0x16, 0x2B }, { 0x17, 0x2D }, { 0x18, 0x2E }, { 0x19, 0x30 }, { 0x1A, 0x31 },
            { 0x1B, 0x32 }, { 0xE, 0x33 }, { 0x23, 0x36 }, { 0x24, 0x37 }, { 0xB, 0x39 }, { 0x25, 0x3C } };
        // the packed fields (func_8001AC44): (offset in the unpacked struct, bits)
        static const std::pair<int, int> FIELDS[] = { { 0, 5 }, { 1, 3 }, { 3, 7 }, { 4, 5 }, { 5, 5 }, { 6, 7 }, { 7, 5 }, { 8, 5 }, { 9, 5 },
            { 0xA, 5 }, { 0xB, 5 }, { 0xC, 4 }, { 0xF, 7 }, { 0x12, 5 }, { 0x13, 3 }, { 0x15, 6 }, { 0x18, 5 }, { 0x1B, 5 }, { 0x1E, 3 },
            { 0x1F, 5 }, { 0x21, 3 }, { 0x22, 5 }, { 0x24, 3 }, { 0x25, 5 }, { 0x27, 3 }, { 0x28, 5 }, { 0x2A, 4 }, { 0x2B, 5 }, { 0x2D, 4 },
            { 0x2E, 5 }, { 0x30, 6 }, { 0x31, 5 }, { 0x32, 5 }, { 0x33, 5 }, { 0x36, 3 }, { 0x37, 5 }, { 0x39, 5 }, { 0x3C, 7 } };
        Bytes un(0x3F);
        for (auto& [s, d] : SRC2UN) un[d] = src[s];
        Costume c; c.nm_un = un;
        static const std::pair<int, const char*> PARTS[] = { { 3, "lower" }, { 6, "upper" }, { 0xF, "face" }, { 0x18, "fh" }, { 0x1B, "mask" } };
        for (auto& [off, part] : PARTS)
            for (auto& [ours, nmv] : NEW[part]) if (nmv == un[off]) { un[off] = (uint8_t)ours; break; }
        Bytes cut = un;
        for (auto& [o, b] : FIELDS) cut[o] &= (uint8_t)((1 << b) - 1);
        Bytes key = van_pack(cut);
        if (key[0x16] & 0x80) fail("ports: a costume's key has bit 183 already");
        key[0x16] |= 0x80;
        if (van_unpack(key) != cut) fail("ports: the key doesn't unpack to its fields");
        c.un = un; c.key = key;
        return c;
    }
    // the appearance format's 38 bytes by name (VPW Studio's names; the game's Create-a-Wrestler lists)
    static int APPF(const std::string& n) {
        static const char* F[] = { "body", "skin", "ring", "ring_c1", "ring_c2", "upper", "upper_c1", "upper_c2", "entr", "entr_c1",
            "entr_c2", "weapon", "gloves", "gloves_c", "tattoo", "wrist", "wrist_c", "lelbow", "lelbow_c", "relbow", "relbow_c", "lknee",
            "lknee_c", "rknee", "rknee_c", "boots", "boots_c1", "boots_c2", "head", "face", "hair", "hair_c", "front_hair", "facial_hair",
            "mask", "hat", "hat_c", "photo" };
        for (int i = 0; i < 38; i++) if (n == F[i]) return i;
        fail("ports: no appearance field %s", n.c_str());
    }
    static Bytes app(Bytes b, std::initializer_list<std::pair<const char*, int>> kw) { for (auto& [k, v] : kw) b[APPF(k)] = (uint8_t)v; return b; }
    static const Bytes& JJ_BASE() {
        static const Bytes b = { 0x02, 0x03, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00,
                                 0x00, 0x00, 0x07, 0x00, 0x08, 0x00, 0x1c, 0x00, 0x00, 0x01, 0x09, 0x09, 0x02, 0x09, 0x00, 0x00, 0x00, 0x00, 0x09 };
        return b;
    }
    Bytes nm_app(int o, int c) {
        int a = EN->r16(NM_STRUCTS + o * 0x50 + 0x14 * c + 0xE);
        const Bytes& A = nmfile(1);
        return Bytes(A.begin() + a * 0x26, A.begin() + a * 0x26 + 0x26);
    }
    Bytes tame(Bytes b, int photo) {
        if (b[2] >= 0x80) {                     // lower body: the game's of the same kind
            uint32_t kind = nm_entry("lower_kind", b[2]), fem = nm_entry("lower_fem", b[2]);
            auto vk = van_entries("lower_kind"), vf = van_entries("lower_fem");
            int i = 0; while (i < 0x80 && !(vk[i] == kind && vf[i] == fem)) i++;
            if (i == 0x80) fail("ports: no lower body of the game's like No Mercy Plus's %d", b[2]);
            b[2] = (uint8_t)i;
        }
        if (b[5] >= 0x7D) b[5] = 0x7D;          // upper: none
        if (b[0x1D] >= 0x76) b[0x1D] = 0;       // face
        b[0x1E] = 2; b[0x20] = 0; b[0x21] = 0; b[0x22] = 0;
        b[0x25] = (uint8_t)photo;
        return b;
    }
    // OVR / OVR2 (No Mercy Plus's whole models: gen/mkovr.py), REV_LATE (the Revenge ports' bodies and photos)
    struct OvrE { const char* key; int o, photo; };
    static const std::vector<OvrE>& OVR() { static const std::vector<OvrE> v = { { "droz", 75, 102 }, { "mideon", 79, 103 } }; return v; }
    static const std::vector<OvrE>& OVR2() {
        static const std::vector<OvrE> v = { { "petegas", 80, 105 }, { "rodney", 81, 106 }, { "joeyabs", 82, 107 }, { "taker99", 83, 108 },
                                             { "raven", 84, 109 }, { "regal", 86, 110 }, { "tiger", 89, 111 } };
        return v;
    }
    struct RevLate { std::string key; int body, photo; };
    static std::vector<RevLate> REV_LATE() {
        std::vector<RevLate> v = { { "brethart", 3, 112 }, { "neidhart", 6, 113 }, { "hogan", 5, 114 }, { "nash", 5, 115 }, { "hall", 2, 116 },
            { "savage", 3, 117 }, { "hennig", 3, 118 }, { "giant", 5, 119 }, { "sting", 4, 120 }, { "luger", 4, 121 }, { "goldberg", 5, 122 },
            { "ricksteiner", 4, 123 }, { "scottsteiner", 4, 124 }, { "booker", 3, 125 }, { "stevieray", 5, 126 }, { "rey", 0, 127 },
            { "piper", 2, 128 }, { "bischoff", 1, 129 } };
        const std::vector<RevLate> base = v;
        for (auto& e : late_table()) if (e.base) for (auto& r : base) if (r.key == e.base) { v.push_back({ e.key, r.body, r.photo }); break; }
        // (no2k15, 2026-11-23: the two WWE 2K15 test slots, last here, removed -- gen/mkparts.py)
        return v;
    }
    std::map<std::string, int> COSTUME0() {
        std::map<std::string, int> c;
        int n0 = 4 * (int)ext_table().size();
        c["droz"] = n0 + 4; c["mideon"] = n0 + 8; c["meanie"] = n0 + 12;
        for (size_t i = 0; i < OVR2().size(); i++) c[OVR2()[i].key] = n0 + 16 + 4 * (int)i;
        auto rl = REV_LATE();
        for (size_t i = 0; i < rl.size(); i++) c[rl[i].key] = n0 + 16 + 4 * (int)OVR2().size() + 4 * (int)i;
        return c;
    }
    std::vector<bool> late_absent;               // (nmplus: all_late_app's rows of No Mercy Plus's men when it isn't there)
    std::vector<Bytes> all_late_app() {
        std::vector<Bytes> v;
        late_absent.clear();
        auto jj = [&](int lower, bool top) {
            return app(JJ_BASE(), { { "body", 3 }, { "ring", lower }, { "upper", top ? 1 : 0 }, { "upper_c1", 1 }, { "upper_c2", 1 }, { "wrist", 1 },
                                    { "wrist_c", 1 }, { "lknee", 1 }, { "lknee_c", 1 }, { "rknee", 1 }, { "rknee_c", 1 }, { "boots", 28 },
                                    { "boots_c1", 0 }, { "boots_c2", 1 }, { "face", 132 }, { "hair", 2 }, { "front_hair", 0 }, { "photo", 101 } });
        };
        v.push_back(jj(143, true)); v.push_back(jj(151, true)); v.push_back(jj(143, false)); v.push_back(jj(151, false));
        late_absent.assign(4, false);
        for (auto& e : OVR()) for (int c = 0; c < 4; c++) {
            if (!NP) { v.push_back(Bytes(0x26)); late_absent.push_back(true); continue; }
            v.push_back(tame(nm_app(e.o, c), e.photo)); late_absent.push_back(false);
        }
        for (int c = 0; c < 4; c++) {
            v.push_back(app(JJ_BASE(), { { "body", 7 }, { "skin", 2 }, { "ring", 111 }, { "ring_c1", 1 }, { "ring_c2", 1 }, { "upper", 36 },
                                         { "upper_c1", c % 2 == 0 ? 2 : 1 }, { "upper_c2", c % 2 == 0 ? 2 : 1 }, { "wrist", 0 }, { "wrist_c", 0 },
                                         { "lknee", 1 }, { "lknee_c", 2 }, { "rknee", 1 }, { "rknee_c", 2 }, { "boots", 1 }, { "boots_c1", 4 },
                                         { "boots_c2", c < 2 ? 4 : 1 }, { "face", 132 }, { "hair", 2 }, { "front_hair", 0 }, { "facial_hair", 0 },
                                         { "mask", 0 }, { "photo", 104 } }));
            late_absent.push_back(false);
        }
        for (auto& e : OVR2()) for (int c = 0; c < 4; c++) {
            if (!NP) { v.push_back(Bytes(0x26)); late_absent.push_back(true); continue; }
            late_absent.push_back(false);
            Bytes a = app(tame(nm_app(e.o, c), e.photo), { { "hat_c", c } });
            if (a[APPF("hat")] != 0) fail("ports: %s wears a hat", e.key);
            v.push_back(a);
        }
        auto rl = REV_LATE();
        std::vector<Bytes> rev;
        for (size_t i = 0; i < rl.size(); i++) for (int c = 0; c < 4; c++)
            rev.push_back(app(JJ_BASE(), { { "body", rl[i].body }, { "ring", 111 }, { "ring_c1", 0 }, { "ring_c2", 0 }, { "upper", 0 }, { "upper_c1", 0 },
                                           { "upper_c2", 0 }, { "wrist", 0 }, { "wrist_c", 0 }, { "lknee", 0 }, { "lknee_c", 0 }, { "rknee", 0 },
                                           { "rknee_c", 0 }, { "boots", 1 }, { "boots_c1", 0 }, { "boots_c2", 0 }, { "face", 132 }, { "hair", 2 },
                                           { "front_hair", 0 }, { "facial_hair", 0 }, { "mask", 0 }, { "hat", 0 }, { "hat_c", 8 + 4 * (int)i + c },
                                           { "photo", rl[i].photo } }));
        // (a second ID has his base's photo: its hat colour, cut to 5 bits in the key, 16 from his)
        auto ix = [&](const std::string& k) { for (size_t i = 0; i < rl.size(); i++) if (rl[i].key == k) return (int)i; fail("ports: %s", k.c_str()); };
        for (auto& e : late_table()) if (e.base)
            for (int c = 0; c < 4; c++) { int r = ix(e.key); rev[4 * r + c] = app(rev[4 * r + c], { { "hat_c", (8 + 4 * ix(e.base) + c + 16) & 31 } }); }
        v.insert(v.end(), rev.begin(), rev.end());
        late_absent.resize(v.size(), false);
        return v;
    }
    static std::string c_bytes(const Bytes& b, size_t per = 24) {
        std::string s;
        for (size_t i = 0; i < b.size(); i += per) {
            if (i) s += ",\n";
            s += "    ";
            for (size_t j = i; j < std::min(b.size(), i + per); j++) s += strf(j > i ? ", 0x%02X" : "0x%02X", b[j]);
        }
        return s;
    }

    Parts mkparts() {
        roster_init();
        EVp = std::make_unique<Emu>(van_img);            // (its own process: No Mercy's image fresh)
        build_tables();
        std::vector<std::string> out = {
            "// generated by gen/mkparts.py from No Mercy Plus V3: the new Superstars' body parts, as more",
            "// entries of the game's part tables (parts.c, gen/patches.py), and their costumes",
            strf("#define LOWER_N 0x%X", LOWER_N), strf("#define UPPER_EXT0 0x%X", UPPER_EXT0),
            strf("#define FACE_EXT0 0x%X", FACE_EXT0), strf("#define FACE_EXT_N %d", FACE_EXT_N),
            strf("#define FH_N 0x%X", FH_N), strf("#define MASK_N 0x%X", MASK_N),
            strf("#define PARAM_EXT0 0x%X", PARAM_EXT0), strf("#define PARAM_EXT_N %d", PARAM_EXT_N), "" };
        for (auto& [name, d] : descs)
            out.push_back(strf("static const u8 pd_%s[%zu] __attribute__((aligned(4))) = {\n", name.c_str(), d.size()) + c_bytes(d) + "\n};");
        for (auto& [name, vals] : tabs) {
            const Tab& t = T().at(name);
            std::vector<std::string> cells;
            for (auto& v : vals) {
                if (!v.pd.empty()) cells.push_back("pd_" + v.pd);
                else if (t.sz == 4) cells.push_back(v.v ? strf("(const void*)0x%08Xu", v.v) : "0");
                else cells.push_back(strf("0x%X", v.v));
            }
            std::string body;
            for (size_t i = 0; i < cells.size(); i += 8) {
                if (i) body += ",\n";
                body += "    ";
                for (size_t j = i; j < std::min(cells.size(), i + 8); j++) body += (j > i ? ", " : "") + cells[j];
            }
            out.push_back(strf("// the game's 0x%08X (%d), then the new", t.va, t.cnt));
            out.push_back(strf("%s parts_%s[%zu] __attribute__((aligned(4))) = {\n", t.sz == 4 ? "const void* const" : (t.sz == 1 ? "const u8" : "const u16"),
                               name.c_str(), vals.size()) + body + "\n};");
        }
        // the costumes
        Parts P;
        std::vector<Bytes> keys, uns, nm_uns;
        std::vector<bool> gone;                          // (nmplus: No Mercy Plus's men when it isn't there -- zero rows)
        for (auto& e : ext_table()) for (int c = 0; c < 4; c++) {
            if (!NP) { keys.push_back(Bytes(0x17)); uns.push_back(Bytes(0x3F)); nm_uns.push_back(Bytes(0x3F)); gone.push_back(true); continue; }
            Costume k = costume_src(nm_app(e.nm, c));
            keys.push_back(k.key); uns.push_back(k.un); nm_uns.push_back(k.nm_un); gone.push_back(false);
        }
        {
            const std::vector<Bytes> late = all_late_app();
            for (size_t i = 0; i < late.size(); i++) {
                if (late_absent[i]) { keys.push_back(Bytes(0x17)); uns.push_back(Bytes(0x3F)); nm_uns.push_back(Bytes(0x3F)); gone.push_back(true); continue; }
                Costume k = costume_src(late[i]); keys.push_back(k.key); uns.push_back(k.un); nm_uns.push_back(k.nm_un); gone.push_back(false);
            }
        }
        {
            std::set<Bytes> seen; size_t n = 0;
            for (size_t i = 0; i < keys.size(); i++) if (!gone[i]) { seen.insert(keys[i]); n++; }
            if (seen.size() != n) fail("ports: two costumes alike");
        }
        out.push_back("// the costumes, as kept (packed, bit 183 set) and unpacked");
        out.push_back(strf("#define EXT_COSTUMES %zu", keys.size()));
        int n0 = 4 * (int)ext_table().size();
        out.push_back(strf("#define COSTUME_JARRETT %d   // (then Droz's, Mideon's, The Blue Meanie's: 4 each)", n0));
        out.push_back(strf("#define COSTUME_DROZ %d", n0 + 4));
        out.push_back(strf("#define COSTUME_MIDEON %d", n0 + 8));
        out.push_back(strf("#define COSTUME_MEANIE %d", n0 + 12));
        auto row = [](const Bytes& b) { std::string s = "    { "; for (size_t i = 0; i < b.size(); i++) s += strf(i ? ", 0x%02X" : "0x%02X", b[i]); return s + " },"; };
        out.push_back("static const u8 ext_looks_key[EXT_COSTUMES][0x17] = {");
        for (auto& k : keys) out.push_back(row(k));
        out.push_back("};");
        out.push_back("static const u8 ext_looks[EXT_COSTUMES][0x3F] = {");
        for (auto& u : uns) out.push_back(row(u));
        out.push_back("};");
        out.push_back("// the files (No Mercy Plus's) at the arcade's IDs");
        for (uint32_t f : used) {
            const Bytes& d = nmfile(f);
            out.push_back(strf("static const u8 pf_%04x[%zu] __attribute__((aligned(8))) = {\n", f, d.size()) + c_bytes(d, 32) + "\n};");
        }
        out.push_back("typedef struct { u16 id; u16 size; const u8* data; } PartFile;");
        out.push_back(strf("#define PART_FILES_N %zu", used.size()));
        out.push_back("static const PartFile part_files[PART_FILES_N] = {");
        for (uint32_t f : used) out.push_back(strf("    { 0x%04X, %zu, pf_%04x },   // No Mercy Plus's %04x", our_file(f), nmfile(f).size(), f, f));
        out.push_back("};");
        std::string txt;
        for (auto& l : out) txt += l + "\n";
        P.parts_ext_h = crlf(txt);
        // parts.pkl's values: dict(keys, uns, nm_uns, files={No Mercy Plus file: ours}), as JSON (hex bytes) for the check
        ojson J = ojson::object(), a = ojson::array(), b = ojson::array(), c = ojson::array(), f = ojson::object();
        for (auto& k : keys) a.push_back(hex2(k.data(), k.size()));
        for (auto& k : uns) b.push_back(hex2(k.data(), k.size()));
        for (auto& k : nm_uns) c.push_back(hex2(k.data(), k.size()));
        for (uint32_t x : used) f[std::to_string(x)] = our_file(x);
        J["keys"] = a; J["uns"] = b; J["nm_uns"] = c; J["files"] = f;
        P.parts_pkl_json = py_dumps(J);
        return P;
    }

    // ==================================================================================================================
    // gen/mkovr.py: parts_ovr.h -- whole models in place of the game's: No Mercy Plus's own (its func_80018A3C run on its
    // image, fresh: the step's own process) for Droz, Mideon and the OVR2 men; WrestleMania 2000's / Revenge's (wmport's
    // models) for Jarrett, the Meanie and the Revenge ports. (no2k15, 2026-11-23: the 2K15 test slots' rows removed)
    // ==================================================================================================================
    std::string parts_ovr_h(const std::vector<std::pair<std::pair<std::string, int>, Bytes>>& wmm) {
        roster_init();
        EVp = std::make_unique<Emu>(van_img);            // (mkparts' module state in the process: fresh, its tables made)
        build_tables();
        std::unique_ptr<Emu> EOp;                        // (mkovr's own EN over the No Mercy Plus image; nmplus: none without it)
        if (NP) EOp = std::make_unique<Emu>(nm_img);
        constexpr uint32_t OVR_FILE0 = 0x4D00, UNO = 0x80781000, OUTB = 0x80782000;
        static const uint32_t GLOB_N[3] = { 0x800A9EA0, 0x800931E4, 0x80099508 };
        static const int STATES[4][4] = { { 0, 0, 0, 0 }, { 1, 1, 3, 0x20 }, { 1, 0, 3, 0 }, { 0, 1, 2, 0x20 } };
        std::set<int> FILE_FIELDS;
        const ojson ffj = read_json_file(data / "frozen" / "model_file_fields.json");   // (kept: the loop reads into it)
        for (auto& v : ffj["fields"]) FILE_FIELDS.insert(v.get<int>());
        auto nm_model = [&](const Bytes& un, const int* st) {
            Emu& EO = *EOp;
            for (int i = 0; i < 3; i++) EO.w8(GLOB_N[i], st[i]);
            Bytes u = un; u.push_back(0);
            EO.wbytes(UNO, u); EO.wbytes(OUTB, Bytes(0x270)); EO.w8(OUTB + 0x254, st[3]);
            EO.call(0x80018ACC, { UNO, OUTB }, {}, 0x807FF000, 3'000'000);
            return EO.rbytes(OUTB, 0x270);
        };
        auto keep_ours = [](int k) { return k >= 0x254 && k < 0x258; };
        std::map<uint32_t, Bytes> files;                 // our ID -> data
        std::map<uint32_t, std::string> names;           // our ID -> "nm <file>"
        struct Model { int costume; Bytes d, mask; int entr; };
        std::vector<Model> models;
        auto nm_differs = [&](uint32_t f) { const Bytes* b = NP->get(f); const Bytes* v = N->get(f); return b && (!v || *b != *v); };
        struct NmModel { std::string name; int c; Bytes m0; std::set<int> vary; int un9; };
        std::vector<NmModel> nm_models;
        std::vector<OvrE> all = OVR(); for (auto& e : OVR2()) all.push_back(e);
        if (NP) for (auto& e : all)
            for (int c = 0; c < 4; c++) {
                std::set<int, std::greater<int>> e9 = { costume_src(nm_app(e.o, c)).nm_un[9], 0 };
                for (int un9 : e9) {
                    Bytes un = costume_src(nm_app(e.o, c)).nm_un; un[9] = (uint8_t)un9;
                    std::vector<Bytes> ms; for (auto& st : STATES) ms.push_back(nm_model(un, st));
                    std::set<int> vary;
                    for (int k = 0; k < 0x270; k++) {
                        if (keep_ours(k)) continue;
                        for (auto& m : ms) if (m[k] != ms[0][k]) { vary.insert(k); break; }
                    }
                    nm_models.push_back({ e.key, c, ms[0], vary, un9 });
                }
            }
        std::set<uint32_t> want;
        for (auto& m : nm_models)
            for (int k = 0; k < 0x270; k += 2) {
                uint32_t v = be16(m.m0, k);
                if (keep_ours(k) || v < 0x800 || v >= 0x7802 || !NP->get(v)) continue;
                bool any = false; for (int j = -1; j < 4; j++) if (nm_differs(v + j)) any = true;
                if (!any) continue;
                if (!FILE_FIELDS.count(k)) fail("ports: %s %d: %04x at %x, not a file's place", m.name.c_str(), m.c, v, k);
                for (int j = -1; j < 4; j++) if (NP->get(v + j)) want.insert(v + j);
            }
        std::map<uint32_t, uint32_t> nm_ours;
        for (uint32_t f : want) {
            if (nm_ours.count(f - 1)) nm_ours[f] = nm_ours[f - 1] + 1;
            else nm_ours[f] = files.empty() ? OVR_FILE0 : files.rbegin()->first + 2;     // (a gap between runs)
            files[nm_ours[f]] = nmfile(f); names[nm_ours[f]] = strf("nm %04x", f);
        }
        auto C0 = COSTUME0();
        // the WrestleMania 2000 / Revenge models' order (below)
        std::vector<std::pair<std::string, int>> wm_order = { { "jarrett", 4 * (int)ext_table().size() } };
        for (const char* k : { "meanie", "brethart", "neidhart", "hogan", "nash", "hall", "savage", "hennig", "giant", "sting", "luger",
                               "goldberg", "ricksteiner", "scottsteiner", "booker", "stevieray", "rey", "piper", "bischoff" })
            wm_order.push_back({ k, C0.at(k) });
        for (auto& e : late_table()) if (e.base) wm_order.push_back({ e.key, C0.at(e.key) });
        if (!NP) {
            // (nmplus) No Mercy Plus isn't there: its models' rows as many as the build has, each for no costume (0xFF) and
            // putting nothing in (its mask zero) -- the rows after them, WrestleMania 2000's and Revenge's, keep their places
            if (!shape) fail("ports: No Mercy Plus V3 isn't there and the build's layouts weren't given (firstrun\hdrfill.bin)");
            const long all_n = (long)hdr_symbol_size(*shape, "ovr_costume"), nm_n = all_n - 4 * (long)wm_order.size();
            if (all_n <= 0 || nm_n < 0) fail("ports: the build's ovr_costume isn't as this importer makes it");
            for (long i = 0; i < nm_n; i++) models.push_back({ 0xFF, Bytes(0x270), Bytes(0x270), -1 });
        }
        for (auto& m : nm_models) {
            Bytes d = m.m0, mask(0x270);
            for (int k = 0; k < 0x270; k += 2) {
                uint32_t v = be16(d, k);
                auto it = nm_ours.find(v);
                if (!keep_ours(k) && it != nm_ours.end() && FILE_FIELDS.count(k)) put_be16(&d[k], it->second);
            }
            for (int k = 0; k < 0x270; k++) mask[k] = (keep_ours(k) || m.vary.count(k)) ? 0 : 1;
            models.push_back({ C0.at(m.name) + m.c, d, mask, m.un9 });
        }
        // WrestleMania 2000's and Revenge's models, whole (wmport's), in the Python's order. (c2hands 2026-11-19) Without
        // WrestleMania 2000, Jarrett's and the Meanie's rows are for no costume (0xFF) and put nothing in, as No Mercy Plus's
        // absent ones: the rows after them keep their places
        for (auto& [name, c0] : wm_order)
            for (int c = 0; c < 4; c++) {
                const Bytes* d = nullptr;
                for (auto& [k, v] : wmm) if (k.first == name && k.second == c) { d = &v; break; }
                // (romsets 2026-11-24: without Revenge its ports' rows are for no costume, likewise; arenasplit 2026-11-24: Jarrett's
                //  and the Meanie's are WrestleMania 2000's alone -- wmport is made from either game)
                const bool wm_man = name == "jarrett" || name == "meanie";
                if (!d && (wm_man ? !wm() : !rev())) { models.push_back({ 0xFF, Bytes(0x270), Bytes(0x270), -1 }); continue; }
                if (!d) fail("ports: wmport has no model for %s costume %d", name.c_str(), c);
                Bytes mask(0x270, 1); mask[0x254] = 0;
                models.push_back({ c0 + c, *d, mask, -1 });
            }
        // out
        auto cb = [](const Bytes& b) { return c_bytes(b, 32); };
        std::vector<std::string> L = { "// generated by gen/mkovr.py: whole models (No Mercy Plus V3's, WrestleMania 2000's) in place",
            "// of the game's (parts.c parts_model_post), and their files",
            strf("#define OVR_N %zu", models.size()), strf("#define OVR_FILE0 0x%X", OVR_FILE0), strf("#define OVR_FILES_N %zu", files.size()),
            strf("#define OVR_FILE_END 0x%X", files.empty() ? OVR_FILE0 : files.rbegin()->first + 1) };
        std::string s;
        for (size_t i = 0; i < models.size(); i++) s += (i ? ", " : "") + std::to_string(models[i].costume);
        L.push_back("static const u8 ovr_costume[OVR_N] = { " + s + " };");
        L.push_back("// the looks' entrance attire (field 0x09) each is for (0xFF: the costume's, whatever it is)");
        s.clear();
        for (size_t i = 0; i < models.size(); i++) s += (i ? ", " : "") + (models[i].entr < 0 ? std::string("0xFF") : std::to_string(models[i].entr));
        L.push_back("static const u8 ovr_entr[OVR_N] = { " + s + " };");
        L.push_back("// each: the model, then which of its bytes are put in (bit per byte)");
        L.push_back("static const u8 ovr_model[OVR_N][0x270] __attribute__((aligned(4))) = {");
        for (auto& m : models) L.push_back("  {\n" + cb(m.d) + "\n  },");
        L.push_back("};");
        L.push_back("static const u8 ovr_mask[OVR_N][0x270 / 8] = {");
        for (auto& m : models) {
            std::string r = "    { ";
            for (int i = 0; i < 0x270; i += 8) { int v = 0; for (int j = 0; j < 8; j++) v += m.mask[i + j] << j; r += strf(i ? ", 0x%02X" : "0x%02X", v); }
            L.push_back(r + " },");
        }
        L.push_back("};");
        for (auto& [fid, b] : files) L.push_back(strf("static const u8 ovf_%04x[%zu] __attribute__((aligned(8))) = {\n", fid, b.size()) + cb(b) + "\n};");
        L.push_back("static const PartFile ovr_files[OVR_FILES_N] = {");
        for (auto& [fid, b] : files) L.push_back(strf("    { 0x%04X, %zu, ovf_%04x },   // %s", fid, b.size(), fid, names.count(fid) ? names[fid].c_str() : "(read with the one before)"));
        L.push_back("};");
        std::string txt;
        for (auto& l : L) txt += l + "\n";
        return crlf(txt);
    }
