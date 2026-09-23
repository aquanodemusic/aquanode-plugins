#include "Alpha3Import.h"
#include "PluginProcessor.h"

namespace Alpha3Import {
namespace {

    // ----------------------------------------------------------
    //  Tokens: int32 values and length-prefixed strings
    // ----------------------------------------------------------
    struct Tok {
        bool isStr = false;
        int v = 0;
        juce::String s;
        juce::String str() const { return isStr ? "'" + s + "'" : juce::String(v); }
    };

    struct Block {
        juce::String tag;
        juce::Array<Tok> toks;
        int intAt(int i, int fallback = 0) const {
            return (i >= 0 && i < toks.size() && !toks[i].isStr) ? toks[i].v : fallback;
        }
        juce::String strAt(int i) const {
            return (i >= 0 && i < toks.size() && toks[i].isStr) ? toks[i].s : juce::String();
        }
        int index() const { return intAt(0, -1); }
    };

    int32_t rdLE(const uint8_t* p) { return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)); }
    bool printable(uint8_t c) { return c >= 32 && c < 127; }

    // Fixed-size, NUL-padded ASCII field -> String
    juce::String fixedString(const uint8_t* p, int maxLen) {
        int len = 0;
        while (len < maxLen && p[len] != 0) ++len;
        return juce::String((const char*)p, (size_t)len);
    }

    // A length word of 4/8/12/16/20 followed by at least two printable
    // characters (then only printable or NUL bytes) is read as a string.
    juce::Array<Tok> tokenize(const uint8_t* p, int n) {
        juce::Array<Tok> out;
        int i = 0;
        while (i + 4 <= n) {
            int v = rdLE(p + i);
            if ((v == 4 || v == 8 || v == 12 || v == 16 || v == 20) && i + 4 + v <= n
                && printable(p[i + 4]) && printable(p[i + 5])) {
                bool ok = true;
                for (int k = 0; k < v; ++k)
                    if (p[i + 4 + k] != 0 && !printable(p[i + 4 + k])) { ok = false; break; }
                if (ok) {
                    Tok t; t.isStr = true;
                    t.s = fixedString(p + i + 4, v);
                    out.add(t);
                    i += 4 + v;
                    continue;
                }
            }
            Tok t; t.v = v; out.add(t);
            i += 4;
        }
        return out;
    }

    bool looksLikeTag(const uint8_t* p) {
        for (int k = 0; k < 4; ++k)
            if (!juce::CharacterFunctions::isLetterOrDigit((juce::juce_wchar)p[k])) return false;
        return true;
    }

    juce::String tagOf(const uint8_t* p) {   // stored reversed: "2GYS" -> "SYG2"
        char t[5] = { (char)p[3], (char)p[2], (char)p[1], (char)p[0], 0 };
        return juce::String(t);
    }

    // Parse one program: starts at the SYG2 block, stops at the next SYG2
    juce::Array<Block> parseBlocks(const uint8_t* data, int n) {
        juce::Array<Block> blocks;
        int start = -1;
        for (int i = 0; i + 4 <= n; ++i)
            if (memcmp(data + i, "2GYS", 4) == 0) { start = i; break; }
        if (start < 0) return blocks;

        int p = start;
        while (p + 8 <= n && looksLikeTag(data + p)) {
            int size = rdLE(data + p + 4);
            if (size < 8 || p + size > n) break;
            Block b;
            b.tag = tagOf(data + p);
            if (b.tag == "SYG2" && !blocks.isEmpty()) break;   // next program (bank)
            b.toks = tokenize(data + p + 8, size - 8);
            blocks.add(b);
            p += size;
        }
        return blocks;
    }

    juce::String squash(const juce::String& s) {       // "Filter$Cutoff " -> "filtercutoff"
        juce::String o;
        for (auto c : s.toLowerCase())
            if (juce::CharacterFunctions::isLetterOrDigit(c)) o += c;
        return o;
    }

    int matchSource(const juce::String& name) {
        auto k = squash(name);
        if (k.isEmpty()) return Mod::SRC_OFF;
        struct { const char* key; int id; } t[] = {
            {"noteplayedlog", Mod::SRC_NOTE_LOG}, {"noteplayedexp", Mod::SRC_NOTE_LOG},
            {"noteplayedlin", Mod::SRC_NOTE_LIN}, {"velocity", Mod::SRC_VELOCITY},
            {"aftertouch", Mod::SRC_AFTERTOUCH}, {"pitchwheel", Mod::SRC_PITCHWHEEL},
            {"modulationwheel", Mod::SRC_MODWHEEL}, {"modwheel", Mod::SRC_MODWHEEL},
            {"breath", Mod::SRC_BREATH}, {"foot", Mod::SRC_FOOT}, {"expression", Mod::SRC_EXPRESSION},
            {"cc16", Mod::SRC_CC16}, {"cc17", Mod::SRC_CC17}, {"cc18", Mod::SRC_CC18}, {"cc19", Mod::SRC_CC19},
            {"ampenv", Mod::SRC_AMP_ENV}, {"filterenv", Mod::SRC_FILT_ENV},
            {"lfo1", Mod::SRC_LFO1}, {"lfo2", Mod::SRC_LFO2}, {"lfo3", Mod::SRC_LFO3},
            {"constant", Mod::SRC_CONSTANT} };
        for (auto& e : t) if (k.startsWith(e.key)) return e.id;
        return -1;
    }

    int matchDest(const juce::String& name) {
        auto k = squash(name);
        if (k.isEmpty()) return Mod::DST_OFF;
        struct { const char* key; int id; } t[] = {
            {"osc1amp", Mod::DST_OSC1_AMP}, {"osc1pitch", Mod::DST_OSC1_PITCH}, {"osc1sym", Mod::DST_OSC1_SYM},
            {"osc2amp", Mod::DST_OSC2_AMP}, {"osc2pitch", Mod::DST_OSC2_PITCH}, {"osc2sym", Mod::DST_OSC2_SYM},
            {"osc2ring", Mod::DST_OSC2_RING}, {"noiseamp", Mod::DST_NOISE_AMP},
            {"filtercutofffm", Mod::DST_CUTOFF_FM}, {"filtercutoff", Mod::DST_CUTOFF},
            {"filterres", Mod::DST_RESONANCE}, {"mainamp", Mod::DST_MAIN_AMP}, {"mainpitch", Mod::DST_MAIN_PITCH},
            {"matrixdepth1", Mod::DST_MDEPTH1}, {"matrixdepth2", Mod::DST_MDEPTH2}, {"matrixdepth3", Mod::DST_MDEPTH3},
            {"lfo1speed", Mod::DST_LFO1_SPEED}, {"lfo2speed", Mod::DST_LFO2_SPEED} };
        for (auto& e : t) if (k.startsWith(e.key)) return e.id;
        return -1;
    }

    // Alpha's 30 waveforms all exist in AlphaBeta (26 of them rebuilt by name)
    int matchWave(const juce::String& name, bool& exact) {
        auto k = squash(name);
        exact = true;
        if (k.startsWith("sine")) return 0;
        if (k.startsWith("tri")) return 1;
        if (k == "sawtooth" || k == "saw") return 2;
        if (k == "square1" || k == "square") return 3;
        if (k.startsWith("noise")) return 4;
        const auto& internal = InternalWaves::names();
        for (int i = 0; i < internal.size(); ++i)
            if (squash(internal[i]) == k) return InternalWaves::FIRST_INDEX + i;
        exact = false;
        if (k.contains("saw")) return 2;
        if (k.startsWith("square")) return 3;
        return 2;
    }

    int matchRange(const juce::String& s) {   // "-1", "+2", or footage "16'"
        auto t = s.trim();
        if (t.containsAnyOf("'\"")) {
            int ft = t.getIntValue();
            switch (ft) { case 32: return -2; case 16: return -1; case 8: return 0; case 4: return 1; case 2: return 2; }
            return 0;
        }
        return juce::jlimit(-2, 2, t.getIntValue());
    }

    int matchFilterType(const juce::String& s) {
        auto k = s.trim().toUpperCase();
        if (k == "LP12" || k == "12" || k == "LP1") return 0;
        if (k == "LP24" || k == "24") return 1;
        if (k == "LP2+" || k == "LP24+" || k == "24+") return 2;
        if (k.startsWith("BP")) return 3;
        if (k.startsWith("HP")) return 4;
        return 1;
    }

    int matchLfoWave(const juce::String& s) {
        auto k = squash(s);
        if (k.startsWith("sin")) return Mod::LFO_SINE;
        if (k.startsWith("tri")) return Mod::LFO_TRI;
        if (k.startsWith("saw")) return Mod::LFO_SAW;
        if (k.startsWith("squ")) return Mod::LFO_SQUARE;
        if (k.startsWith("noi")) return Mod::LFO_NOISE;
        if (k.startsWith("sam") || k.startsWith("sh")) return Mod::LFO_SAMHO;
        return Mod::LFO_TRI;
    }

    // ----------------------------------------------------------
    //  Writer: sets parameters and logs what it did
    // ----------------------------------------------------------
    struct Writer {
        juce::AudioProcessorValueTreeState& apvts;
        juce::StringArray log;

        void setReal(const juce::String& id, float value, const juce::String& conf, const juce::String& what) {
            if (auto* p = apvts.getParameter(id)) {
                p->setValueNotifyingHost(p->convertTo0to1(value));
                log.add("[" + conf + "] " + what + "  ->  " + id + " = " + p->getCurrentValueAsText());
            }
        }
        void setNorm(const juce::String& id, float norm, const juce::String& conf, const juce::String& what) {
            if (auto* p = apvts.getParameter(id)) {
                p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, norm));
                log.add("[" + conf + "] " + what + "  ->  " + id + " = " + p->getCurrentValueAsText());
            }
        }
        void note(const juce::String& s) { log.add("        " + s); }
    };

    void resetToDefaults(juce::AudioProcessorValueTreeState& apvts) {
        for (auto* param : apvts.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(param))
                rp->setValueNotifyingHost(rp->getDefaultValue());
    }

    // ----------------------------------------------------------
    //  Block interpreters
    // ----------------------------------------------------------

    // SYG2: 6 ints, then 24 source names, 24 amounts (1/100 %), 24 dest names
    void applyMatrix(const Block& b, Writer& w) {
        int i = 0;
        while (i < b.toks.size() && !b.toks[i].isStr) ++i;
        juce::StringArray src, dst; juce::Array<int> amt;
        while (i < b.toks.size() && b.toks[i].isStr) src.add(b.toks[i++].s);
        while (i < b.toks.size() && !b.toks[i].isStr) amt.add(b.toks[i++].v);
        while (i < b.toks.size() && b.toks[i].isStr) dst.add(b.toks[i++].s);
        int rows = juce::jmin(src.size(), dst.size(), amt.size());
        if (rows == 0) { w.note("matrix: not found"); return; }

        juce::Array<int> used;
        for (int r = 0; r < rows; ++r)
            if (matchSource(src[r]) != Mod::SRC_OFF || matchDest(dst[r]) != Mod::DST_OFF) used.add(r);

        // Keep row positions if they fit (Matrix Depth 1-3 refers to positions).
        // In the factory patch rows 0-1 are empty, so the visible slots start at row 2.
        int offset = 2;
        bool fits = !used.isEmpty() && used.getFirst() >= offset && used.getLast() < offset + Mod::NUM_SLOTS;
        if (!fits && !used.isEmpty() && used.getLast() - used.getFirst() < Mod::NUM_SLOTS) {
            offset = used.getFirst(); fits = true;
        }
        w.note("matrix: " + juce::String(rows) + " rows stored, " + juce::String(used.size()) + " in use, "
               + (fits ? "row offset " + juce::String(offset) : juce::String("compacted")));

        int slot = 0;
        for (int r : used) {
            int target = fits ? r - offset : slot++;
            if (target < 0 || target >= Mod::NUM_SLOTS) { w.note("matrix row " + juce::String(r) + " dropped (no free slot)"); continue; }
            int s = matchSource(src[r]), d = matchDest(dst[r]);
            juce::String row = "matrix row " + juce::String(r) + ": " + src[r].trim() + " | "
                             + juce::String(amt[r] / 100.0, 2) + "% | " + dst[r].trim();
            if (s < 0) { w.note(row + "  (unknown source, slot left off)"); s = Mod::SRC_OFF; }
            if (d < 0) { w.note(row + "  (unknown destination, slot left off)"); d = Mod::DST_OFF; }
            w.setReal(Mod::slotSrcID(target), (float)s, "HIGH", row);
            w.setReal(Mod::slotAmtID(target), (float)amt[r] / 100.0f, "HIGH", "  amount");
            w.setReal(Mod::slotDstID(target), (float)d, "HIGH", "  destination");
        }
    }

    // TM01 (per oscillator): idx, ver, waveA, rangeA, waveB, rangeB, balance 0..100
    void applyOsc(const Block& b, Writer& w) {
        int o = b.index();
        if (o != 0 && o != 1) return;
        bool o1 = (o == 0);
        auto name = juce::String("osc") + juce::String(o + 1);
        bool exA, exB;
        int wa = matchWave(b.strAt(2), exA), wb = matchWave(b.strAt(4), exB);
        w.setReal(o1 ? PID::O1WA : PID::O2WA, (float)wa, exA ? "HIGH" : "APPROX", name + " wave A '" + b.strAt(2).trim() + "'");
        w.setReal(o1 ? PID::O1OCA : PID::O2OCA, (float)(matchRange(b.strAt(3)) + 2), "HIGH", name + " range A '" + b.strAt(3).trim() + "'");
        w.setReal(o1 ? PID::O1WB : PID::O2WB, (float)wb, exB ? "HIGH" : "APPROX", name + " wave B '" + b.strAt(4).trim() + "'");
        w.setReal(o1 ? PID::O1OCB : PID::O2OCB, (float)(matchRange(b.strAt(5)) + 2), "HIGH", name + " range B '" + b.strAt(5).trim() + "'");
        w.setReal(o1 ? PID::O1MRP : PID::O2MRP, (float)b.intAt(6) / 100.0f, "HIGH", name + " A bal B " + juce::String(b.intAt(6)));
    }

    // VFS0: idx, ver, ?, type, cutoff (0..1000?), reso (0..100)
    void applyFilter(const Block& b, Writer& w) {
        w.setReal(PID::FTYP, (float)matchFilterType(b.strAt(3)), "HIGH", "filter type '" + b.strAt(3) + "'");
        w.setNorm(PID::FCUT, (float)b.intAt(4) / 1000.0f, "MEDIUM", "filter cutoff raw " + juce::String(b.intAt(4)) + " (dial position /1000)");
        w.setReal(PID::FRES, (float)b.intAt(5) / 100.0f, "HIGH", "filter reso raw " + juce::String(b.intAt(5)));
    }

    // VD01: idx, ver, ?, drive 0..100, mode string
    void applyDrive(const Block& b, Writer& w) {
        w.setReal(PID::DRV, (float)b.intAt(3) / 100.0f, "HIGH", "drive raw " + juce::String(b.intAt(3)) + " (" + b.strAt(4).trim() + " mode not modelled)");
    }

    // ME03: idx (0 = filter, 1 = amp), ver, ?, A, D, S?, R  (times 0..10000 dial)
    void applyEnv(const Block& b, Writer& w) {
        bool filt = b.index() == 0;
        auto n = juce::String(filt ? "filter env" : "amp env");
        int A = b.intAt(3), D = b.intAt(4), S = b.intAt(5), R = b.intAt(6);
        w.setNorm(filt ? PID::FATT : PID::AATT, A / 10000.0f, "MEDIUM", n + " attack raw " + juce::String(A));
        w.setNorm(filt ? PID::FDEC : PID::ADEC, D / 10000.0f, "MEDIUM", n + " decay raw " + juce::String(D));
        w.setReal(filt ? PID::FSUS : PID::ASUS, juce::jlimit(0.0f, 1.0f, (S + 100) / 200.0f), "LOW", n + " sustain raw " + juce::String(S) + " (assumed -100..100)");
        w.setNorm(filt ? PID::FREL : PID::AREL, R / 10000.0f, "MEDIUM", n + " release raw " + juce::String(R));
    }

    // ML02 idx 3..5 = LFO 1..3: idx, ver, mode?, wave, freq (Hz*100?), ?, sync
    void applyLfo(const Block& b, Writer& w) {
        int k = b.index() - 3;
        if (k < 0 || k >= Mod::NUM_LFOS) return;
        auto n = "lfo " + juce::String(k + 1);
        w.setReal(Mod::lfoWaveID(k), (float)matchLfoWave(b.strAt(3)), "HIGH", n + " wave '" + b.strAt(3) + "'");
        w.setReal(Mod::lfoRateID(k), juce::jlimit(0.01f, 32.0f, b.intAt(4) / 100.0f), "LOW", n + " freq raw " + juce::String(b.intAt(4)) + " (assumed Hz*100)");
        int sync = Mod::syncNames().indexOf(b.strAt(6).trim());
        w.setReal(Mod::lfoSyncID(k), (float)juce::jmax(0, sync), "HIGH", n + " sync '" + b.strAt(6).trim() + "'");
    }

    // MRG0: idx, ver, 'BEND', bend, time 0..100, 'Time'/'Rate', mode
    void applyGlide(const Block& b, Writer& w) {
        auto mode = b.strAt(6).trim();
        if (mode.equalsIgnoreCase("Off"))
            w.setReal(PID::GLID, 0.0f, "MEDIUM", "glide mode 'Off'");
        else
            w.setNorm(PID::GLID, b.intAt(4) / 100.0f, "LOW", "glide mode '" + mode + "' time raw " + juce::String(b.intAt(4)));
    }
}

// ==============================================================
Result importFile(const juce::File& file, juce::AudioProcessorValueTreeState& apvts) {
    Result res;
    juce::MemoryBlock mb;
    if (!file.loadFileAsData(mb) || mb.getSize() < 60) {
        res.summary = "IMPORT FAILED: cannot read file";
        return res;
    }
    auto* d = (const uint8_t*)mb.getData();
    int n = (int)mb.getSize();

    if (memcmp(d, "CcnK", 4) != 0) { res.summary = "IMPORT FAILED: not an fxp/fxb file"; return res; }
    juce::String magic((const char*)d + 8, 4), fxID((const char*)d + 16, 4);
    int dataStart = 0;
    if (magic == "FPCh") {
        res.patchName = fixedString(d + 28, 28).trim();
        dataStart = 60;
    } else if (magic == "FBCh") {
        dataStart = 160;   // bank chunk; the first program is imported
    } else {
        res.summary = "IMPORT FAILED: '" + magic + "' presets are not supported (need a chunk preset)";
        return res;
    }
    if (fxID != "LwAF")
        res.report << "Warning: plugin ID is '" << fxID << "', Alpha 3 uses 'LwAF'.\n";

    auto blocks = parseBlocks(d + dataStart, n - dataStart);
    if (blocks.isEmpty()) { res.summary = "IMPORT FAILED: no Alpha 3 data found"; return res; }
    if (res.patchName.isEmpty()) res.patchName = file.getFileNameWithoutExtension();

    resetToDefaults(apvts);
    Writer w{ apvts, {} };

    for (auto& b : blocks) {
        if (b.tag == "SYG2")      applyMatrix(b, w);
        else if (b.tag == "TM01") applyOsc(b, w);
        else if (b.tag == "VFS0") applyFilter(b, w);
        else if (b.tag == "VD01") applyDrive(b, w);
        else if (b.tag == "ME03") applyEnv(b, w);
        else if (b.tag == "ML02") applyLfo(b, w);
        else if (b.tag == "MRG0") applyGlide(b, w);
    }

    // ---- Report ----
    juce::String rep;
    rep << "AlphaBeta - Alpha 3 import report\n"
        << "File:  " << file.getFullPathName() << "\n"
        << "Patch: " << res.patchName << "   (" << magic << ", id " << fxID << ", "
        << blocks.size() << " blocks)\n\n"
        << "MAPPED VALUES  (HIGH = format understood, MEDIUM = scale guessed,\n"
        << "                LOW = meaning guessed, APPROX = nearest AlphaBeta equivalent)\n"
        << w.log.joinIntoString("\n") << "\n\n"
        << "RAW BLOCKS  (for calibrating the unknown fields)\n";
    for (auto& b : blocks) {
        juce::StringArray t;
        for (auto& tk : b.toks) t.add(tk.str());
        if (b.tag == "SYG2") {   // matrix lists are shown above; print head + tail
            juce::StringArray head, tail;
            for (int k = 0; k < juce::jmin(6, t.size()); ++k) head.add(t[k]);
            int tailStart = t.size();   // tail = everything after the last "- - -" entry
            for (int k = t.size() - 1; k >= 0; --k)
                if (b.toks[k].isStr && b.toks[k].s.contains("- - -")) { tailStart = k + 1; break; }
            for (int k = tailStart; k < t.size(); ++k) tail.add(t[k]);
            rep << b.tag << "  head: " << head.joinIntoString(" ") << "\n      tail: " << tail.joinIntoString(" ") << "\n";
        } else {
            rep << b.tag << "  " << t.joinIntoString(" ") << "\n";
        }
    }
    rep << "\nNot yet decoded: osc mix, noise, detune, ringmod, filter env depth, fade,\n"
        << "amp volume/velocity, chorus, spread (probably in VCH0 / GOD2 / SYG2 tail).\n";

    res.report = res.report + rep;
    res.ok = true;
    res.summary = "IMPORTED " + res.patchName.toUpperCase() + "  (see report)";
    return res;
}

} // namespace Alpha3Import
