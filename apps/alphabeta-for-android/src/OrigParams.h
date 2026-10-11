#pragma once
// ============================================================================
//  Parameters of the ORIGINAL engine mode (model of LinPlug FreeAlpha 3.3)
//
//  Every continuous control is stored as the original's own 0..1 knob value,
//  so the knob travel (and host automation) follows the original laws. The
//  display text shows the original's units (ms, Hz, cents, dB ...).
//  IDs start with "a3_" and never collide with the native engine's IDs.
// ============================================================================
#include <JuceHeader.h>
#include "AlphaOrigEngine.h"

namespace OrigP {

    inline const juce::String ENGINE = "engine";             // 0 = Native, 1 = Original

    inline juce::String waveID(int osc, int ab)  { return "a3_o" + juce::String(osc + 1) + (ab ? "_wave_b" : "_wave_a"); }
    inline juce::String rangeID(int osc, int ab) { return "a3_o" + juce::String(osc + 1) + (ab ? "_range_b" : "_range_a"); }
    inline juce::String mixID(int osc)           { return "a3_o" + juce::String(osc + 1) + "_mix"; }
    // env: 0 = amp, 1 = filter; stage: 0 attack, 1 decay, 2 sustain, 3 fade, 4 release
    inline juce::String envID(int env, int stage) {
        static const char* st[5] = { "att", "dec", "sus", "fade", "rel" };
        return juce::String(env == 0 ? "a3_amp_" : "a3_flt_") + st[stage];
    }
    inline juce::String rowSrcID(int i) { return "a3_mm" + juce::String(i + 1) + "_src"; }
    inline juce::String rowAmtID(int i) { return "a3_mm" + juce::String(i + 1) + "_amt"; }
    inline juce::String rowDstID(int i) { return "a3_mm" + juce::String(i + 1) + "_dst"; }

    inline const juce::String BAL = "a3_balance", DET = "a3_detune", SPR = "a3_spread", DRV = "a3_drive",
        FTYP = "a3_f_type", FCUT = "a3_f_cut", FRES = "a3_f_res", FDEP = "a3_f_depth",
        VOL = "a3_volume", VEL = "a3_velocity",
        GMODE = "a3_glide_mode", GTIME = "a3_glide_time", GBEND = "a3_glide_bend",
        VOICES = "a3_voices", CWET = "a3_ch_wet", CTIM = "a3_ch_time", CRAT = "a3_ch_rate",
        LWAVE = "a3_lfo_wave", LRATE = "a3_lfo_rate", LSYNC = "a3_lfo_sync", LATT = "a3_lfo_att";

    // True for every parameter that belongs to the original engine (or the
    // engine switch). The native randomizer / importer leave these alone.
    inline bool isOrigID(const juce::String& id) { return id.startsWith("a3_") || id == ENGINE; }

    // ---- knob <-> display laws (the original's) ----
    namespace K {
        using namespace AlphaOrig;
        inline double envTimeInv(double s)   { return invExpMap(s, 0.001, 10.0, 4.0); }
        inline double fadeInv(double f)      { return juce::jlimit(0.0, 1.0, (f + 10.0) / 20.0); }
        inline double lfoHzInv(double hz)    { return invExpMap(hz, 0.01, 32.0, 4.0); }
        inline double lfoAttInv(double s)    { return invExpMap(s, 0.0, 10.0, 4.0); }
        inline double glideTimeInv(double s) { return invExpMap(s, 0.001, 4.0, 5.0); }
        inline double spreadInv(double c)    { return invExpMap(c, 0.0, 50.0, 4.0); }
        inline double detuneInv(double c)    { return invExpMap(c, 0.0, 100.0, 5.0); }
        inline double chorusMsInv(double ms) { return invExpMap(ms, 0.01, 6.0, 4.0); }
        inline double chorusHzInv(double hz) { return invExpMap(hz, 0.02, 4.0, 4.0); }
    }

    namespace Fmt {
        inline juce::String time(double s) {
            if (s < 0.01)  return juce::String(s * 1000.0, 1) + " ms";
            if (s < 1.0)   return juce::String(juce::roundToInt(s * 1000.0)) + " ms";
            return juce::String(s, 2) + " s";
        }
        inline juce::String hz(double f) {
            if (f >= 1000.0) return juce::String(f / 1000.0, 2) + " kHz";
            if (f >= 100.0)  return juce::String(juce::roundToInt(f)) + " Hz";
            return juce::String(f, 2) + " Hz";
        }
        inline juce::String pct(double v) { return juce::String(juce::roundToInt(v * 100.0)) + " %"; }
    }

    // ------------------------------------------------------------------------
    //  Parameter layout
    // ------------------------------------------------------------------------
    inline void addParameters(std::vector<std::unique_ptr<juce::RangedAudioParameter>>& p) {
        using namespace AlphaOrig;
        auto names = [](const char* const* list, int n) { juce::StringArray a; for (int i = 0; i < n; ++i) a.add(list[i]); return a; };
        auto choice = [&](const juce::String& id, const juce::String& nm, const juce::StringArray& items, int def) {
            p.push_back(std::make_unique<juce::AudioParameterChoice>(id, nm, items, def));
        };
        auto knob = [&](const juce::String& id, const juce::String& nm, double def, std::function<juce::String(float)> fmt,
                        juce::NormalisableRange<float> r = { 0.0f, 1.0f }) {
            p.push_back(std::make_unique<juce::AudioParameterFloat>(id, nm, r, (float)def,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction([fmt](float v, int) { return fmt(v); })));
        };

        const Patch init;   // the original's init patch
        choice(ENGINE, "Engine", { "Native", "Original" }, 0);

        const auto waves = names(AlphaOrigData::kWaveNames, AlphaOrigData::kNumWaves);
        const auto ranges = names(kRangeNames, 5);
        for (int o = 0; o < 2; ++o) {
            const auto on = "A3 Osc " + juce::String(o + 1);
            choice(waveID(o, 0), on + " Wave A", waves, init.wave[o][0]);
            choice(rangeID(o, 0), on + " Range A", ranges, init.range[o][0] + 2);
            choice(waveID(o, 1), on + " Wave B", waves, init.wave[o][1]);
            choice(rangeID(o, 1), on + " Range B", ranges, init.range[o][1] + 2);
            knob(mixID(o), on + " Mix", init.mix[o], [](float v) { return "B " + Fmt::pct(v); });
        }
        knob(BAL, "A3 Osc Balance", init.balance, [](float v) { return "Osc 2 " + Fmt::pct(v); });
        knob(DET, "A3 Detune", K::detuneInv(init.detune), [](float v) { return juce::String(Law::detune(v), 1) + " ct"; });
        knob(SPR, "A3 Spread", K::spreadInv(init.spread), [](float v) { return juce::String(Law::spread(v), 2) + " ct"; });
        knob(DRV, "A3 Drive", init.drive, [](float v) { return Fmt::pct(v); });

        choice(FTYP, "A3 Filter Type", names(kFilterNames, 5), init.filterType);
        knob(FCUT, "A3 Cutoff", init.cutoff, [](float v) { return Fmt::hz(Law::cutoffHz(v)); });
        knob(FRES, "A3 Resonance", init.resonance, [](float v) { return Fmt::pct(v); });
        knob(FDEP, "A3 Env Depth", init.envDepth, [](float v) { return (v > 0 ? "+" : "") + Fmt::pct(v); }, { -1.0f, 1.0f });

        for (int e = 0; e < 2; ++e) {
            const EnvParams& ep = e == 0 ? init.ampEnv : init.filEnv;
            const juce::String en = e == 0 ? "A3 Amp " : "A3 Filter ";
            knob(envID(e, 0), en + "Attack", K::envTimeInv(ep.attack), [](float v) { return Fmt::time(Law::envTime(v)); });
            knob(envID(e, 1), en + "Decay", K::envTimeInv(ep.decay), [](float v) { return Fmt::time(Law::envTime(v)); });
            knob(envID(e, 2), en + "Sustain", Law::invSustain(ep.sustain), [](float v) { return Fmt::pct(Law::sustain(v)); });
            knob(envID(e, 3), en + "Fade", K::fadeInv(ep.fade), [](float v) { auto f = Law::fade(v); return (f > 0 ? "+" : "") + juce::String(f, 2); });
            knob(envID(e, 4), en + "Release", K::envTimeInv(ep.release), [](float v) { return Fmt::time(Law::envTime(v)); });
        }

        knob(VOL, "A3 Volume", init.volume, [](float v) { return v <= 0.0f ? juce::String("-inf dB") : juce::String(Law::volumeDb(v), 1) + " dB"; });
        knob(VEL, "A3 Velocity", Law::invVelocity(init.velSens), [](float v) { return Fmt::pct(Law::velocity(v)); });
        choice(GMODE, "A3 Glide Mode", names(kGlideModeNames, 4), init.glideMode);
        knob(GTIME, "A3 Glide Time", K::glideTimeInv(init.glideTime), [](float v) { return Fmt::time(Law::glideTime(v)); });
        knob(GBEND, "A3 Glide Bend", init.glideBend, [](float v) { int s = juce::roundToInt(v); return (s > 0 ? "+" : "") + juce::String(s) + " st"; },
             { -48.0f, 48.0f, 1.0f });
        choice(VOICES, "A3 Voices", names(kVoicesNames, 13), init.voices == 0 ? 12 : init.voices - 1);

        knob(CWET, "A3 Chorus Wet", init.chorusWet, [](float v) { return Fmt::pct(v); });
        knob(CTIM, "A3 Chorus Time", K::chorusMsInv(init.chorusMs), [](float v) { return juce::String(Law::chorusMs(v), 2) + " ms"; });
        knob(CRAT, "A3 Chorus Rate", K::chorusHzInv(init.chorusHz), [](float v) { return Fmt::hz(Law::chorusHz(v)); });

        choice(LWAVE, "A3 LFO Wave", names(kLfoWaveNames, 6), init.lfoWave);
        knob(LRATE, "A3 LFO Rate", K::lfoHzInv(init.lfoHz), [](float v) { return Fmt::hz(Law::lfoHz(v)); });
        choice(LSYNC, "A3 LFO Sync", names(kSyncNames, kNumSync), 0);
        knob(LATT, "A3 LFO Attack", K::lfoAttInv(init.lfoAttack), [](float v) { return Fmt::time(Law::lfoAttack(v)); });

        const auto srcs = names(kSrcNames, NUM_SRC), dsts = names(kDstNames, NUM_DST);
        for (int i = 0; i < kNumRows; ++i) {
            auto n = juce::String(i + 1);
            choice(rowSrcID(i), "A3 Matrix " + n + " Source", srcs, 0);
            p.push_back(std::make_unique<juce::AudioParameterFloat>(rowAmtID(i), "A3 Matrix " + n + " Amount",
                juce::NormalisableRange<float>(-100.0f, 100.0f, 0.01f), 0.0f));
            choice(rowDstID(i), "A3 Matrix " + n + " Dest", dsts, 0);
        }
    }

    // ------------------------------------------------------------------------
    //  Audio thread: raw parameter pointers -> AlphaOrig::Patch
    // ------------------------------------------------------------------------
    struct Cache {
        using AP = std::atomic<float>*;
        AP wave[2][2]{}, range[2][2]{}, mix[2]{}, bal{}, det{}, spr{}, drv{}, ftyp{}, fcut{}, fres{}, fdep{},
           env[2][5]{}, vol{}, vel{}, gmode{}, gtime{}, gbend{}, voices{}, cwet{}, ctim{}, crat{},
           lwave{}, lrate{}, lsync{}, latt{}, src[AlphaOrig::kNumRows]{}, amt[AlphaOrig::kNumRows]{}, dst[AlphaOrig::kNumRows]{};

        void init(juce::AudioProcessorValueTreeState& s) {
            auto g = [&](const juce::String& id) { auto* p = s.getRawParameterValue(id); jassert(p != nullptr); return p; };
            for (int o = 0; o < 2; ++o) {
                for (int ab = 0; ab < 2; ++ab) { wave[o][ab] = g(waveID(o, ab)); range[o][ab] = g(rangeID(o, ab)); }
                mix[o] = g(mixID(o));
            }
            bal = g(BAL); det = g(DET); spr = g(SPR); drv = g(DRV);
            ftyp = g(FTYP); fcut = g(FCUT); fres = g(FRES); fdep = g(FDEP);
            for (int e = 0; e < 2; ++e) for (int st = 0; st < 5; ++st) env[e][st] = g(envID(e, st));
            vol = g(VOL); vel = g(VEL); gmode = g(GMODE); gtime = g(GTIME); gbend = g(GBEND); voices = g(VOICES);
            cwet = g(CWET); ctim = g(CTIM); crat = g(CRAT);
            lwave = g(LWAVE); lrate = g(LRATE); lsync = g(LSYNC); latt = g(LATT);
            for (int i = 0; i < AlphaOrig::kNumRows; ++i) { src[i] = g(rowSrcID(i)); amt[i] = g(rowAmtID(i)); dst[i] = g(rowDstID(i)); }
        }

        AlphaOrig::Patch build(double tempo) const {
            using namespace AlphaOrig;
            auto f = [](AP p) { return (double)p->load(); };
            auto n = [](AP p) { return juce::roundToInt(p->load()); };
            Patch P;
            for (int o = 0; o < 2; ++o) {
                for (int ab = 0; ab < 2; ++ab) {
                    P.wave[o][ab] = juce::jlimit(0, AlphaOrigData::kNumWaves - 1, n(wave[o][ab]));
                    P.range[o][ab] = juce::jlimit(-2, 2, n(range[o][ab]) - 2);
                }
                P.mix[o] = f(mix[o]);
            }
            P.balance = f(bal); P.detune = Law::detune(f(det)); P.spread = Law::spread(f(spr)); P.drive = f(drv);
            P.filterType = juce::jlimit(0, 4, n(ftyp)); P.cutoff = f(fcut); P.resonance = f(fres); P.envDepth = f(fdep);
            for (int e = 0; e < 2; ++e) {
                EnvParams& ep = e == 0 ? P.ampEnv : P.filEnv;
                ep.attack = Law::envTime(f(env[e][0])); ep.decay = Law::envTime(f(env[e][1]));
                ep.sustain = Law::sustain(f(env[e][2])); ep.fade = Law::fade(f(env[e][3]));
                ep.release = Law::envTime(f(env[e][4]));
            }
            P.volume = f(vol); P.velSens = Law::velocity(f(vel));
            P.glideMode = juce::jlimit(0, 3, n(gmode)); P.glideTime = Law::glideTime(f(gtime)); P.glideBend = std::round(f(gbend));
            const int vi = juce::jlimit(0, 12, n(voices)); P.voices = vi == 12 ? 0 : vi + 1;
            P.chorusWet = f(cwet); P.chorusMs = Law::chorusMs(f(ctim)); P.chorusHz = Law::chorusHz(f(crat));
            P.lfoWave = juce::jlimit(0, 5, n(lwave)); P.lfoHz = Law::lfoHz(f(lrate));
            P.lfoSync = juce::jlimit(0, kNumSync - 1, n(lsync)); P.lfoAttack = Law::lfoAttack(f(latt));
            for (int i = 0; i < kNumRows; ++i) {
                P.rows[(size_t)i].src = juce::jlimit(0, NUM_SRC - 1, n(src[i]));
                P.rows[(size_t)i].dst = juce::jlimit(0, NUM_DST - 1, n(dst[i]));
                P.rows[(size_t)i].amount = juce::jlimit(-1.0, 1.0, f(amt[i]) / 100.0);
            }
            P.tempo = tempo > 0.0 ? tempo : 120.0;
            return P;
        }
    };

    // ------------------------------------------------------------------------
    //  Message thread: AlphaOrig::Patch (e.g. from an imported .fxp) -> params
    // ------------------------------------------------------------------------
    inline void applyPatch(juce::AudioProcessorValueTreeState& s, const AlphaOrig::Patch& P) {
        using namespace AlphaOrig;
        auto set = [&](const juce::String& id, double realValue) {
            if (auto* prm = s.getParameter(id)) {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost(prm->convertTo0to1((float)realValue));
                prm->endChangeGesture();
            }
        };
        for (int o = 0; o < 2; ++o) {
            for (int ab = 0; ab < 2; ++ab) { set(waveID(o, ab), P.wave[o][ab]); set(rangeID(o, ab), P.range[o][ab] + 2); }
            set(mixID(o), P.mix[o]);
        }
        set(BAL, P.balance); set(DET, K::detuneInv(P.detune)); set(SPR, K::spreadInv(P.spread)); set(DRV, P.drive);
        set(FTYP, P.filterType); set(FCUT, P.cutoff); set(FRES, P.resonance); set(FDEP, P.envDepth);
        for (int e = 0; e < 2; ++e) {
            const EnvParams& ep = e == 0 ? P.ampEnv : P.filEnv;
            set(envID(e, 0), K::envTimeInv(ep.attack)); set(envID(e, 1), K::envTimeInv(ep.decay));
            set(envID(e, 2), Law::invSustain(ep.sustain)); set(envID(e, 3), K::fadeInv(ep.fade));
            set(envID(e, 4), K::envTimeInv(ep.release));
        }
        set(VOL, P.volume); set(VEL, Law::invVelocity(P.velSens));
        set(GMODE, P.glideMode); set(GTIME, K::glideTimeInv(P.glideTime)); set(GBEND, P.glideBend);
        set(VOICES, P.voices == 0 ? 12 : juce::jlimit(1, 12, P.voices) - 1);
        set(CWET, P.chorusWet); set(CTIM, K::chorusMsInv(P.chorusMs)); set(CRAT, K::chorusHzInv(P.chorusHz));
        set(LWAVE, P.lfoWave); set(LRATE, K::lfoHzInv(P.lfoHz)); set(LSYNC, P.lfoSync); set(LATT, K::lfoAttInv(P.lfoAttack));
        for (int i = 0; i < kNumRows; ++i) {
            set(rowSrcID(i), P.rows[(size_t)i].src);
            set(rowAmtID(i), P.rows[(size_t)i].amount * 100.0);
            set(rowDstID(i), P.rows[(size_t)i].dst);
        }
    }
}
