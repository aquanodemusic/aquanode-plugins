#pragma once
// ============================================================================
//  Alpha 3 / FreeAlpha preset (.fxp program chunk) reader -> AlphaOrig::Patch
//
//  The chunk is a list of tagged blocks (4CC stored reversed, little-endian
//  size including the 8-byte header). Field meanings were established by
//  comparing all 179 factory presets with the values the original plugin
//  reports for them; every field below reproduces the plugin exactly.
// ============================================================================
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>
#include "AlphaOrigEngine.h"
#include "AlphaOrigTemplates.h"

namespace AlphaOrig {

struct PresetResult {
    bool ok = false;
    std::string name, error;
    Patch patch;
};

namespace detail {
    inline int32_t rd32(const uint8_t* p) { return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)); }
    inline uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

    struct Reader {
        const uint8_t* p; size_t n, o = 0;
        Reader(const uint8_t* d, size_t len, size_t off = 0) : p(d), n(len), o(off) {}
        bool can(size_t k) const { return o + k <= n; }
        int32_t i() { if (!can(4)) { o = n; return 0; } int32_t v = rd32(p + o); o += 4; return v; }
        std::string s() {
            int32_t len = i(); if (len < 0 || !can((size_t)len)) { o = n; return {}; }
            std::string r((const char*)p + o, (size_t)len); o += (size_t)len;
            size_t z = r.find('\0'); if (z != std::string::npos) r.resize(z);
            return r;
        }
    };
    inline std::string trim(std::string s) {
        while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
        size_t b = 0; while (b < s.size() && s[b] == ' ') ++b;
        return s.substr(b);
    }
    inline std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }

    inline int waveIndex(const std::string& name) {
        std::string k = lower(trim(name));
        for (int i = 0; i < AlphaOrigData::kNumWaves; ++i) if (lower(AlphaOrigData::kWaveNames[i]) == k) return i;
        for (int i = 0; i < AlphaOrigData::kNumWaves; ++i) if (lower(AlphaOrigData::kWaveNames[i]).compare(0, 6, k.substr(0, 6)) == 0) return i;
        return 2;
    }
    inline int rangeValue(const std::string& raw) {           // "16\"" footage or "-1", "+2", " 0 "
        std::string s = trim(raw);
        if (s == "32\"") return -2;
        if (s == "16\"") return -1;
        if (s == "8\"") return 0;
        if (s == "4\"") return 1;
        if (s == "2\"") return 2;
        try { int v = std::stoi(s); return std::max(-2, std::min(2, v)); } catch (...) { return 0; }
    }
    inline int indexOf(const char* const* list, int n, const std::string& s) {
        std::string k = trim(s);
        for (int i = 0; i < n; ++i) if (k == list[i]) return i;
        return -1;
    }
    // Matrix names: the display splits long names over two lines with '$'
    // ("Filter$Cutoff") and older presets may hold a shortened name ("Note").
    // The original accepts all of these; compare without '$' and repeated
    // spaces, then fall back to a prefix match. Unknown names -> -1.
    inline std::string squash(const std::string& s) {
        std::string r; bool sp = false;
        for (char c : s) {
            if (c == '$' || c == ' ' || c == '\0') { sp = true; continue; }
            if (sp && !r.empty()) r += ' ';
            sp = false; r += c;
        }
        return r;
    }
    inline int matrixIndex(const char* const* list, int n, const std::string& s) {
        const std::string k = squash(s);
        if (k.empty()) return -1;
        for (int i = 0; i < n; ++i) if (squash(list[i]) == k) return i;
        for (int i = 1; i < n; ++i) if (squash(list[i]).compare(0, k.size(), k) == 0) return i;
        return -1;
    }
}

// chunk: the program data of an FPCh preset (after the 60-byte fxp header)
inline PresetResult readChunk(const uint8_t* d, size_t n) {
    using namespace detail;
    PresetResult res; Patch& P = res.patch;
    // locate first block
    size_t p = 0; bool found = false;
    for (; p + 8 <= n; ++p) if (std::memcmp(d + p, "2GYS", 4) == 0) { found = true; break; }
    if (!found) { res.error = "no Alpha data in chunk"; return res; }

    // Two chunk generations exist: presets saved by FreeAlpha 3.3 itself have a
    // 64-byte MRG0 block (60-byte VCH0); older Alpha 3 presets have 48 bytes.
    // The older ones store note-source amounts at 4/3 and LFO-speed amounts at
    // double scale.
    bool newFormat = false;
    for (size_t q = p; q + 8 <= n; ) {
        int32_t sz = rd32(d + q + 4);
        if (sz < 8 || q + (size_t)sz > n) break;
        if (std::memcmp(d + q, "0GRM", 4) == 0) newFormat = sz >= 64;
        q += (size_t)sz;
    }

    static const char* const ftNames[5] = { "LP12", "LP24", "LP2+", "BP12", "HP12" };
    static const char* const lfoNames[6] = { "Sine", "Triang", "Sawtoo", "Square", "Noise", "Sam&Ho" };
    static const char* const glideNames[4] = { "OFF", "HELD", "ON", "BEND" };

    while (p + 8 <= n) {
        char tag[5] = { (char)d[p + 3], (char)d[p + 2], (char)d[p + 1], (char)d[p], 0 };
        int32_t size = rd32(d + p + 4);
        if (size < 8 || p + (size_t)size > n) break;
        const uint8_t* b = d + p + 8; size_t bn = (size_t)size - 8;
        std::string t(tag);
        Reader r(b, bn);
        if (t == "SYG2") {
            r.i(); r.i(); res.name = trim(r.s());
            int32_t volume5 = r.i(); r.i();               // volume in 1/5 dB
            P.volume = std::max(0.0, std::min(1.0, (91.0 + volume5) / 181.0));   // knob: 181 steps of 0.2 dB
            int srcIdx[24] = {}, dstIdx[24] = {}; std::string dstName[24];
            for (int k = 0; k < 24; ++k) { int32_t len = r.i(); std::string s((const char*)b + r.o, 16); r.o += (size_t)len; srcIdx[k] = matrixIndex(kSrcNames, NUM_SRC, s); }
            int32_t amt[24]; for (int k = 0; k < 24; ++k) amt[k] = r.i();
            for (int k = 0; k < 24; ++k) { int32_t len = r.i(); std::string s((const char*)b + r.o, 16); r.o += (size_t)len; dstName[k] = trim(s); dstIdx[k] = matrixIndex(kDstNames, NUM_DST, s); }
            for (int k = 0; k < kNumRows; ++k) {
                Row row; row.src = std::max(0, srcIdx[k]); row.dst = std::max(0, dstIdx[k]);
                double a = amt[k] / 10000.0;
                if (!newFormat) {
                    if (row.dst == DST_LFO_SPEED) a *= 0.5;                               // stored at double scale
                    if (row.src == SRC_NOTE_LIN || row.src == SRC_NOTE_LOG) a *= 0.75;    // stored at 4/3 scale
                }
                row.amount = std::max(-1.0, std::min(1.0, a));
                P.rows[(size_t)k] = row;
            }
            // tail: locate the filter-type string, other fields sit at fixed offsets around it
            size_t tail = r.o;
            for (size_t q = tail; q + 12 <= bn; ++q) {
                if (rd32(b + q) == 8 && indexOf(ftNames, 5, std::string((const char*)b + q + 4, 4)) >= 0) {
                    if (q >= tail + 32) P.envDepth = std::max(-1.0, std::min(1.0, rd32(b + q - 32) / 100.0));
                    P.detune  = std::max(0.0, std::min(100.0, rd32(b + q - 12) / 10.0));
                    P.balance = std::max(0.0, std::min(1.0, rd32(b + q - 8) / 100.0));
                    if (q + 20 <= bn) P.spread = std::max(0.0, std::min(50.0, rd32(b + q + 16) / 100.0));   // 1/100 cent
                    Reader tr(b, bn, q + 12); for (int k = 0; k < 5; ++k) tr.i();
                    std::string vs = trim(tr.s());
                    P.voices = vs == "all" ? 0 : (vs == "mono" ? 1 : std::max(1, std::min(12, std::atoi(vs.c_str()))));
                    tr.s(); tr.i(); tr.i(); tr.i();
                    int vel = tr.i(); P.velSens = std::max(0, std::min(100, vel)) / 100.0;
                    break;
                }
            }
        } else if (t == "TM01") {
            int idx = r.i(); r.i();
            if (idx == 0 || idx == 1) {
                P.wave[idx][0] = waveIndex(r.s()); P.range[idx][0] = rangeValue(r.s());
                P.wave[idx][1] = waveIndex(r.s()); P.range[idx][1] = rangeValue(r.s());
                P.mix[idx] = std::max(0, std::min(100, (int)r.i())) / 100.0;
            }
        } else if (t == "VFS0") {
            r.i(); r.i(); r.i();
            int ft = indexOf(ftNames, 5, r.s()); P.filterType = ft < 0 ? 0 : ft;
            int cut = r.i(); P.cutoff = invExpMap(std::max(40, std::min(20000, cut)), 40.0, 20000.0, 4.0);
            P.resonance = std::max(0, std::min(100, (int)r.i())) / 100.0;
        } else if (t == "ME03") {
            int idx = r.i(); r.i();
            EnvParams e; e.attack = std::max(1, (int)r.i()) / 1000.0; e.decay = std::max(1, (int)r.i()) / 1000.0;
            e.sustain = std::max(0, std::min(1000, (int)r.i())) / 1000.0; e.fade = std::max(-1000, std::min(1000, (int)r.i())) / 100.0;
            e.release = std::max(1, (int)r.i()) / 1000.0;
            if (idx == 0) P.ampEnv = e; else if (idx == 1) P.filEnv = e;
        } else if (t == "ML01") {
            int idx = r.i(); r.i(); r.i();
            if (idx == 0) {
                int w = indexOf(lfoNames, 6, r.s()); P.lfoWave = w < 0 ? 1 : w;
                P.lfoHz = std::max(1, std::min(3200, (int)r.i())) / 100.0; r.i();
                int s = indexOf(kSyncNames, kNumSync, r.s()); P.lfoSync = s < 0 ? 0 : s;
                r.i(); P.lfoAttack = std::max(0, std::min(10000, (int)r.i())) / 1000.0;
            }
        } else if (t == "MRG0") {
            r.i(); r.i();
            std::string m = trim(r.s()); for (auto& c : m) c = (char)std::toupper((unsigned char)c);
            int gm = indexOf(glideNames, 4, m); P.glideMode = gm < 0 ? 0 : gm;
            P.glideBend = std::max(-48, std::min(48, (int)r.i()));
            P.glideTime = std::max(1, std::min(4000, (int)r.i())) / 1000.0;
        } else if (t == "VD01") {
            r.i(); r.i(); r.i(); P.drive = std::max(0, std::min(100, (int)r.i())) / 100.0;
        } else if (t == "VCH0") {
            r.i(); r.i(); r.i();
            P.chorusMs = std::max(1, std::min(600, (int)r.i())) / 100.0; r.i();
            P.chorusHz = std::max(20, std::min(4000, (int)r.i())) / 1000.0; r.i();
            P.chorusWet = std::max(0, std::min(100, (int)r.i())) / 100.0;
        }
        p += (size_t)size;
    }
    res.ok = true;
    return res;
}

// whole .fxp file (FPCh). Returns an error for other preset types.
inline PresetResult readFxp(const uint8_t* d, size_t n) {
    using namespace detail;
    PresetResult res;
    if (n < 60 || std::memcmp(d, "CcnK", 4) != 0) { res.error = "not an .fxp file"; return res; }
    if (std::memcmp(d + 8, "FPCh", 4) != 0) {
        if (std::memcmp(d + 8, "FBCh", 4) == 0) {                 // bank: take the first program
            for (size_t q = 0; q + 4 <= n; ++q)
                if (std::memcmp(d + q, "2GYS", 4) == 0) return readChunk(d + q, n - q);
            res.error = "bank without Alpha data"; return res;
        }
        res.error = "only chunk presets (FPCh) are supported"; return res;
    }
    uint32_t sz = be32(d + 56); if (60 + (size_t)sz > n) sz = (uint32_t)(n - 60);
    res = readChunk(d + 60, sz);
    if (res.name.empty()) { char nm[29] = {}; std::memcpy(nm, d + 28, 28); res.name = trim(nm); }
    return res;
}


// ============================================================================
//  Writer: AlphaOrig::Patch -> FreeAlpha program chunk / .fxp / .fxb
//
//  Patches a program chunk saved by FreeAlpha 3.3 itself (AlphaOrigTemplates.h)
//  in place, so the result has exactly the structure the original expects.
//  Every field is the inverse of readChunk(); strings use the encodings
//  found in the original's own presets.
// ============================================================================
namespace detail {
    inline void wr32(std::vector<uint8_t>& d, size_t o, int32_t v) {
        if (o + 4 > d.size()) return;
        d[o] = (uint8_t)(v & 0xff); d[o + 1] = (uint8_t)((v >> 8) & 0xff); d[o + 2] = (uint8_t)((v >> 16) & 0xff); d[o + 3] = (uint8_t)((v >> 24) & 0xff);
    }
    inline void wrBE32(std::vector<uint8_t>& d, size_t o, uint32_t v) {
        d[o] = (uint8_t)(v >> 24); d[o + 1] = (uint8_t)(v >> 16); d[o + 2] = (uint8_t)(v >> 8); d[o + 3] = (uint8_t)v;
    }
    inline void wrStr(std::vector<uint8_t>& d, size_t o, size_t field, const std::string& s) {   // fixed-size field
        if (o + field > d.size()) return;
        for (size_t i = 0; i < field; ++i) d[o + i] = i < s.size() ? (uint8_t)s[i] : 0;
    }
    inline int iround(double v) { return (int)std::lround(v); }
    inline std::string pad(std::string s, size_t n) { while (s.size() < n) s += ' '; return s.substr(0, n); }
    inline std::string waveStr(int w) { return pad(AlphaOrigData::kWaveNames[std::max(0, std::min(AlphaOrigData::kNumWaves - 1, w))], 8); }
    inline std::string rangeStr(int r) {
        static const char* f[5] = { "32\"", "16\"", " 8\"", " 4\"", " 2\"" };
        return f[std::max(-2, std::min(2, r)) + 2];
    }
    inline std::string lfoWaveStr(int w) {
        static const char* n[6] = { "Sine  ", "Triang", "Sawtoo", "Square", "Noise ", "Sam&Ho" };
        return n[std::max(0, std::min(5, w))];
    }
    inline std::string syncStr(int i) {
        i = std::max(0, std::min(kNumSync - 1, i));
        if (i == 0) return " Off ";
        std::string n = kSyncNames[i]; char suffix = ' ';
        if (!n.empty() && (n.back() == '*' || n.back() == 'T')) { suffix = n.back(); n.pop_back(); }
        if (n == "1/64") return " 1/64";                                  // as the original writes it
        while (n.size() < 4) n = " " + n;
        return n + suffix;
    }
    inline std::string glideStr(int m) { static const char* n[4] = { "Off ", "Held", " On ", "Bend" }; return n[std::max(0, std::min(3, m))]; }
    inline std::string filterStr(int t) { static const char* n[5] = { "LP12", "LP24", "LP2+", "BP12", "HP12" }; return n[std::max(0, std::min(4, t))]; }
    inline std::string voicesStr(int v) {
        if (v <= 0) return " all";
        if (v == 1) return "mono";
        std::string n = std::to_string(std::min(12, v));
        while (n.size() < 3) n = " " + n;
        return n + " ";
    }
    inline std::string matrixStr(const char* name) {
        std::string n = name;
        if (n == "- - -") return "      - - -     ";
        return pad(n, 16);
    }
    struct BlockRef { std::string tag; size_t pos, size; };
    inline std::vector<BlockRef> listBlocks(const std::vector<uint8_t>& d) {
        std::vector<BlockRef> v; size_t p = 0;
        for (; p + 8 <= d.size(); ++p) if (std::memcmp(d.data() + p, "2GYS", 4) == 0) break;
        while (p + 8 <= d.size()) {
            int32_t sz = rd32(d.data() + p + 4);
            if (sz < 8 || p + (size_t)sz > d.size()) break;
            std::string t{ (char)d[p + 3], (char)d[p + 2], (char)d[p + 1], (char)d[p] };
            v.push_back({ t, p, (size_t)sz }); p += (size_t)sz;
        }
        return v;
    }
}

// Program chunk for a patch. The name is stored in the chunk (and in the .fxp header).
inline std::vector<uint8_t> writeChunk(const Patch& P, const std::string& nameIn) {
    using namespace detail;
    std::vector<uint8_t> d(AlphaOrigTemplates::kProgram, AlphaOrigTemplates::kProgram + sizeof(AlphaOrigTemplates::kProgram));
    std::string name = nameIn.substr(0, 23);

    // ---- SYG2: rebuild with the new name (its field length varies) ----
    {
        auto bl = listBlocks(d);
        if (bl.empty() || bl[0].tag != "SYG2") return d;
        const size_t p = bl[0].pos, b = p + 8;
        const int32_t oldLen = rd32(d.data() + b + 8);
        const size_t newLen = (name.size() / 4 + 1) * 4;
        std::vector<uint8_t> nameField(4 + newLen, 0);
        wr32(nameField, 0, (int32_t)newLen);
        for (size_t i = 0; i < name.size(); ++i) nameField[4 + i] = (uint8_t)name[i];
        std::vector<uint8_t> out(d.begin(), d.begin() + (long)(b + 8));
        out.insert(out.end(), nameField.begin(), nameField.end());
        out.insert(out.end(), d.begin() + (long)(b + 12 + (size_t)oldLen), d.end());
        const int32_t newSize = (int32_t)bl[0].size + (int32_t)newLen - oldLen;
        wr32(out, p + 4, newSize);
        d.swap(out);
    }
    for (const auto& blk : listBlocks(d)) {
        const size_t b = blk.pos + 8, bn = blk.size - 8;
        auto at = [&](size_t off) { return b + off; };
        if (blk.tag == "SYG2") {
            size_t o = at(8); o += 4 + (size_t)rd32(d.data() + o);              // skip name
            wr32(d, o, iround(P.volume * 181.0 - 91.0)); o += 8;                  // volume (1/5 dB), then one int
            for (int k = 0; k < 24; ++k) {                                        // sources
                const int32_t len = rd32(d.data() + o);
                const Row* r = k < kNumRows ? &P.rows[(size_t)k] : nullptr;
                wrStr(d, o + 4, (size_t)len, matrixStr(kSrcNames[r ? std::max(0, std::min(NUM_SRC - 1, r->src)) : 0]));
                o += 4 + (size_t)len;
            }
            for (int k = 0; k < 24; ++k) {                                        // amounts as displayed (FreeAlpha format)
                const Row* r = k < kNumRows ? &P.rows[(size_t)k] : nullptr;
                wr32(d, o, r ? iround(std::max(-1.0, std::min(1.0, r->amount)) * 10000.0) : 0); o += 4;
            }
            for (int k = 0; k < 24; ++k) {                                        // destinations
                const int32_t len = rd32(d.data() + o);
                const Row* r = k < kNumRows ? &P.rows[(size_t)k] : nullptr;
                wrStr(d, o + 4, (size_t)len, matrixStr(kDstNames[r ? std::max(0, std::min(NUM_DST - 1, r->dst)) : 0]));
                o += 4 + (size_t)len;
            }
            // tail fields around the filter-type string
            for (size_t q = o; q + 72 <= b + bn; ++q) {
                static const char* const ft[5] = { "LP12", "LP24", "LP2+", "BP12", "HP12" };
                if (rd32(d.data() + q) == 8 && indexOf(ft, 5, std::string((const char*)d.data() + q + 4, 4)) >= 0) {
                    wr32(d, q - 32, iround(P.envDepth * 100.0));
                    wr32(d, q - 12, iround(P.detune * 10.0));
                    wr32(d, q - 8, iround(P.balance * 100.0));
                    wrStr(d, q + 4, 8, filterStr(P.filterType));
                    wr32(d, q + 16, iround(P.spread * 100.0));
                    wrStr(d, q + 36, 8, voicesStr(P.voices));
                    wr32(d, q + 68, iround(P.velSens * 100.0));
                    break;
                }
            }
        } else if (blk.tag == "TM01") {
            const int idx = rd32(d.data() + b);
            if (idx == 0 || idx == 1) {
                wrStr(d, at(12), 12, waveStr(P.wave[idx][0]));
                wrStr(d, at(28), 4, rangeStr(P.range[idx][0]));
                wrStr(d, at(36), 12, waveStr(P.wave[idx][1]));
                wrStr(d, at(52), 4, rangeStr(P.range[idx][1]));
                wr32(d, at(56), iround(P.mix[idx] * 100.0));
            }
        } else if (blk.tag == "VCH0") {
            wr32(d, at(12), iround(P.chorusMs * 100.0));
            wr32(d, at(20), iround(P.chorusHz * 1000.0));
            wr32(d, at(28), iround(P.chorusWet * 100.0));
        } else if (blk.tag == "ML02") {
            // the chorus sweeps: rates stored at 0.01 Hz resolution, 1.0 / 0.7 / 1.3 x the chorus rate
            const int idx = rd32(d.data() + b);
            static const float mult[4] = { 1.0f, 0.7f, 1.3f, 1.0f };
            if (idx >= 3 && idx <= 6) { const float v = (float)P.chorusHz * mult[idx - 3] * 100.0f; wr32(d, at(24), (int)v); }
        } else if (blk.tag == "ML01") {
            if (rd32(d.data() + b) == 0) {
                wrStr(d, at(16), 8, lfoWaveStr(P.lfoWave));
                wr32(d, at(24), iround(P.lfoHz * 100.0));
                wrStr(d, at(36), 8, syncStr(P.lfoSync));
                wr32(d, at(48), iround(P.lfoAttack * 1000.0));
            }
        } else if (blk.tag == "ME03") {
            const int idx = rd32(d.data() + b);
            const EnvParams* e = idx == 0 ? &P.ampEnv : (idx == 1 ? &P.filEnv : nullptr);
            if (e) {
                wr32(d, at(8), std::max(1, iround(e->attack * 1000.0)));
                wr32(d, at(12), std::max(1, iround(e->decay * 1000.0)));
                wr32(d, at(16), iround(e->sustain * 1000.0));
                wr32(d, at(20), iround(e->fade * 100.0));
                wr32(d, at(24), std::max(1, iround(e->release * 1000.0)));
            }
        } else if (blk.tag == "MRG0") {
            wrStr(d, at(12), 8, glideStr(P.glideMode));
            wr32(d, at(20), iround(P.glideBend));
            wr32(d, at(24), std::max(1, iround(P.glideTime * 1000.0)));
        } else if (blk.tag == "VD01") {
            wr32(d, at(12), iround(P.drive * 100.0));
        } else if (blk.tag == "VFS0") {
            wrStr(d, at(16), 8, filterStr(P.filterType));
            wr32(d, at(24), iround(Law::cutoffHz(P.cutoff)));
            wr32(d, at(28), iround(P.resonance * 100.0));
        }
    }
    return d;
}

// .fxp (FPCh) with the program chunk
inline std::vector<uint8_t> writeFxp(const Patch& P, const std::string& name) {
    using namespace detail;
    const auto chunk = writeChunk(P, name);
    std::vector<uint8_t> f(60, 0);
    std::memcpy(f.data(), "CcnK", 4); wrBE32(f, 4, (uint32_t)(52 + chunk.size()));
    std::memcpy(f.data() + 8, "FPCh", 4); wrBE32(f, 12, 1);
    std::memcpy(f.data() + 16, "LwAF", 4); wrBE32(f, 20, 1); wrBE32(f, 24, 1);
    for (size_t i = 0; i < 27 && i < name.size(); ++i) f[28 + i] = (uint8_t)name[i];
    wrBE32(f, 56, (uint32_t)chunk.size());
    f.insert(f.end(), chunk.begin(), chunk.end());
    return f;
}

// .fxb (FBCh): FreeAlpha's bank = its global blocks + one PROG block holding the patch
inline std::vector<uint8_t> writeFxb(const Patch& P, const std::string& name) {
    using namespace detail;
    const auto chunk = writeChunk(P, name);
    std::vector<uint8_t> bank(AlphaOrigTemplates::kBankHeader, AlphaOrigTemplates::kBankHeader + sizeof(AlphaOrigTemplates::kBankHeader));
    std::vector<uint8_t> prog(12, 0);
    std::memcpy(prog.data(), "GORP", 4); wr32(prog, 4, (int32_t)(12 + chunk.size())); wr32(prog, 8, 0);
    bank.insert(bank.end(), prog.begin(), prog.end());
    bank.insert(bank.end(), chunk.begin(), chunk.end());
    std::vector<uint8_t> f(160, 0);
    std::memcpy(f.data(), "CcnK", 4); wrBE32(f, 4, (uint32_t)(152 + bank.size()));
    std::memcpy(f.data() + 8, "FBCh", 4); wrBE32(f, 12, 1);
    std::memcpy(f.data() + 16, "LwAF", 4); wrBE32(f, 20, 1); wrBE32(f, 24, 208);
    wrBE32(f, 156, (uint32_t)bank.size());
    f.insert(f.end(), bank.begin(), bank.end());
    return f;
}

} // namespace AlphaOrig
