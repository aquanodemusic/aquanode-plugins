#pragma once
// ============================================================================
//  TRANSFER between the two engines: translates every setting that has a
//  counterpart, using the measured laws of the original engine and the
//  formulas of the native one (cutoff as the real filter frequency, envelope
//  times matched at their half-way point, matrix amounts rescaled, ...).
//  Settings without a counterpart (FM, noise, ring, filter FM, LFO 2/3 ...)
//  are left out.
// ============================================================================
#include "PluginProcessor.h"

namespace EngineTransfer {
namespace detail {
    inline void set(juce::AudioProcessorValueTreeState& s, const juce::String& id, float v) {
        if (auto* p = s.getParameter(id)) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(v)); p->endChangeGesture(); }
    }
    inline float get(juce::AudioProcessorValueTreeState& s, const juce::String& id) { return s.getRawParameterValue(id)->load(); }

    // wave names: original list <-> native list
    inline int origToNativeWave(int w) {
        juce::String n = AlphaOrigData::kWaveNames[juce::jlimit(0, AlphaOrigData::kNumWaves - 1, w)];
        if (n == "Triangle") n = "Tri"; else if (n == "Sawtooth") n = "Saw"; else if (n == "Square1") n = "Square";
        const int i = InternalWaves::allWaveNames().indexOf(n);
        return i >= 0 ? i : 2;
    }
    inline int nativeToOrigWave(int w) {
        juce::String n = InternalWaves::allWaveNames()[w];
        if (n == "Tri") n = "Triangle"; else if (n == "Saw" || n == "Noise" || n == "Table") n = "Sawtooth"; else if (n == "Square") n = "Square1";
        for (int i = 0; i < AlphaOrigData::kNumWaves; ++i) if (n == AlphaOrigData::kWaveNames[i]) return i;
        return 2;
    }
    // real filter frequency of the original's coefficient (LP24+ sits lower)
    inline double origCutHz(double knob, int type) {
        const double F = std::min(1.0, AlphaOrig::cutoffCoef(AlphaOrig::knobToU(knob)));
        double f = 44100.0 / AlphaOrig::kPi * std::asin(0.5 * F);
        if (type == 2) f *= 0.76;
        return f;
    }
    inline double hzToOrigKnob(double hz, int type) {
        if (type == 2) hz /= 0.76;
        const double F = 2.0 * std::sin(AlphaOrig::kPi * std::min(hz, 7350.0) / 44100.0);
        double lo = 0.0, hi = 0.992;                           // invert the coefficient curve
        for (int i = 0; i < 40; ++i) { const double m = 0.5 * (lo + hi); (AlphaOrig::cutoffCoef(m) < F ? lo : hi) = m; }
        return AlphaOrig::invExpMap(78.125 * std::exp2(8.0 * lo), 40.0, 20000.0, 4.0);
    }
    // fade: original = seconds to fade (10 - |f|), native = level change rate
    inline double origFadeToNative(double f, double sus) {
        if (std::abs(f) < 0.05) return 0.5;
        const double span = f > 0 ? 1.0 - sus : sus, t = std::max(0.05, 10.0 - std::abs(f));
        return juce::jlimit(0.0, 1.0, 0.5 + (f > 0 ? 1.0 : -1.0) * (span / t) / 0.6);
    }
    inline double nativeFadeToOrig(double nf, double sus) {
        const double v = std::abs(nf - 0.5) * 0.6;
        if (v < 0.006) return 0.0;
        const double span = nf > 0.5 ? 1.0 - sus : sus, t = span / v;
        return (nf > 0.5 ? 1.0 : -1.0) * juce::jlimit(0.05, 10.0, 10.0 - std::min(t, 9.95));
    }
    inline double invPitchMod(double semis) {                 // inverse of AlphaOrig::pitchModSemis
        double lo = -1.0, hi = 1.0;
        for (int i = 0; i < 40; ++i) { const double m = 0.5 * (lo + hi); (AlphaOrig::pitchModSemis(m) < semis ? lo : hi) = m; }
        return 0.5 * (lo + hi);
    }
    // matrix maps (original index <-> native index) and amount factors (native % per original unit)
    inline int srcO2N(int s) {
        static const int m[AlphaOrig::NUM_SRC] = { Mod::SRC_OFF, Mod::SRC_NOTE_LIN, Mod::SRC_NOTE_LOG, Mod::SRC_VELOCITY, Mod::SRC_AFTERTOUCH,
            Mod::SRC_PITCHWHEEL, Mod::SRC_MODWHEEL, Mod::SRC_AMP_ENV, Mod::SRC_FILT_ENV, Mod::SRC_LFO1, Mod::SRC_CONSTANT };
        return m[juce::jlimit(0, AlphaOrig::NUM_SRC - 1, s)];
    }
    inline int dstO2N(int d) {
        using namespace AlphaOrig;
        switch (d) {
        case DST_OSC1_AMP: return Mod::DST_OSC1_AMP;   case DST_OSC1_PITCH: return Mod::DST_OSC1_PITCH; case DST_OSC1_SYM: return Mod::DST_OSC1_SYM;
        case DST_OSC2_AMP: return Mod::DST_OSC2_AMP;   case DST_OSC2_PITCH: return Mod::DST_OSC2_PITCH; case DST_OSC2_SYM: return Mod::DST_OSC2_SYM;
        case DST_CUTOFF: return Mod::DST_CUTOFF;       case DST_RESONANCE: return Mod::DST_RESONANCE;   case DST_MAIN_AMP: return Mod::DST_MAIN_AMP;
        case DST_MAIN_PITCH: return Mod::DST_MAIN_PITCH; case DST_DEPTH1: return Mod::DST_MDEPTH1;      case DST_DEPTH2: return Mod::DST_MDEPTH2;
        case DST_DEPTH3: return Mod::DST_MDEPTH3;      case DST_LFO_SPEED: return Mod::DST_LFO1_SPEED;
        default: return Mod::DST_OFF;
        }
    }
    inline double srcFactor(int origSrc) {
        using namespace AlphaOrig;
        return (origSrc == SRC_NOTE_LIN || origSrc == SRC_NOTE_LOG) ? 1.25 : (origSrc == SRC_VELOCITY ? 2.0 : 1.0);
    }
    inline double dstFactor(int origDst) {
        using namespace AlphaOrig;
        switch (origDst) {
        case DST_OSC1_PITCH: case DST_OSC2_PITCH: case DST_MAIN_PITCH: return 48.18 / Mod::PITCH_SEMIS_FULL;
        case DST_OSC1_SYM: case DST_OSC2_SYM: return 0.482 / 0.45;
        case DST_CUTOFF: return 8.0 / Mod::CUTOFF_OCT_FULL;
        default: return 1.0;
        }
    }
    constexpr double kAtt = 0.78, kDec = 0.41;               // linear vs. power-law segments, equal half-way time
    constexpr double kVolRef = 0.7;                          // native volume that matches 0 dB in the original
}

// ---- ORIGINAL -> NATIVE ----
inline juce::String originalToNative(juce::AudioProcessorValueTreeState& s) {
    using namespace detail; using namespace AlphaOrig;
    OrigP::Cache c; c.init(s); const Patch P = c.build(120.0);
    for (auto* prm : s.processor.getParameters())             // native patch back to defaults first
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(prm)) {
            const auto id = rp->getParameterID();
            if (OrigP::isOrigID(id) || id == PID::BEND || id == PID::TUNE || id.contains("wt_pos")) continue;
            rp->setValueNotifyingHost(rp->getDefaultValue());
        }
    const juce::String W[2][2] = { { PID::O1WA, PID::O1WB }, { PID::O2WA, PID::O2WB } }, O[2][2] = { { PID::O1OCA, PID::O1OCB }, { PID::O2OCA, PID::O2OCB } };
    for (int o = 0; o < 2; ++o) for (int ab = 0; ab < 2; ++ab) { set(s, W[o][ab], (float)origToNativeWave(P.wave[o][ab])); set(s, O[o][ab], (float)(P.range[o][ab] + 2)); }
    set(s, PID::O1MRP, (float)P.mix[0]); set(s, PID::O2MRP, (float)P.mix[1]);
    set(s, PID::MIX, (float)P.balance); set(s, PID::DRV, (float)P.drive); set(s, PID::FM, 0.0f); set(s, PID::NOISE, 0.0f); set(s, PID::RING, 0.0f);
    const double detC = 4.015 * std::floor(P.detune * 10.0 + 1e-6) / 10.0;     // cents each way
    set(s, PID::O1DET, (float)detC); set(s, PID::O2DET, (float)-detC);
    set(s, PID::UNI, (float)juce::jlimit(0.0, 1.0, P.spread * 1.73 / 30.0));
    set(s, PID::FTYP, (float)P.filterType);
    set(s, PID::FCUT, (float)juce::jlimit(20.0, 20000.0, origCutHz(P.cutoff, P.filterType)));
    set(s, PID::FRES, (float)P.resonance);
    const double u0 = knobToU(P.cutoff);
    set(s, PID::FDEP, (float)juce::jlimit(-1.0, 1.0, P.envDepth >= 0 ? 2.0 * P.envDepth * (1.0 - u0) : 2.0 * P.envDepth * u0));
    auto env = [&](const EnvParams& e, const juce::String& a, const juce::String& d, const juce::String& su, const juce::String& r, const juce::String& f) {
        set(s, a, (float)juce::jlimit(0.001, 10.0, e.attack * kAtt)); set(s, d, (float)juce::jlimit(0.001, 10.0, e.decay * kDec));
        set(s, su, (float)e.sustain); set(s, r, (float)juce::jlimit(0.001, 10.0, e.release * kDec)); set(s, f, (float)origFadeToNative(e.fade, e.sustain));
    };
    env(P.ampEnv, PID::AATT, PID::ADEC, PID::ASUS, PID::AREL, PID::AFAD);
    env(P.filEnv, PID::FATT, PID::FDEC, PID::FSUS, PID::FREL, PID::FFAD);
    const double db = P.volume <= 0.0 ? -60.0 : Law::volumeDb(P.volume);
    set(s, PID::AVOL, (float)juce::jlimit(0.0, 1.0, kVolRef * std::pow(10.0, db / 20.0)));
    set(s, PID::AVEL, (float)P.velSens);
    const double cw = P.chorusWet;
    set(s, PID::HWET, (float)(2.0 * cw / (1.0 + 2.0 * cw)));
    set(s, PID::HTIM, (float)juce::jlimit(1.0, 50.0, 0.18 + 1.2 * P.chorusMs));
    set(s, PID::HRAT, (float)juce::jlimit(0.1, 10.0, P.chorusHz));
    const bool mono = P.voices == 1;
    set(s, PID::VMODE, mono ? (P.glideMode == 1 ? 2.0f : 1.0f) : 0.0f);
    set(s, PID::GLID, P.glideMode != 0 ? (float)juce::jlimit(0.0, 2.0, P.glideTime) : 0.0f);
    set(s, Mod::lfoWaveID(0), (float)P.lfoWave); set(s, Mod::lfoRateID(0), (float)juce::jlimit(0.01, 32.0, P.lfoHz));
    set(s, Mod::lfoSyncID(0), (float)P.lfoSync); set(s, Mod::lfoAttID(0), (float)P.lfoAttack); set(s, Mod::lfoModeID(0), 1.0f);
    // matrix; constant pitch offsets become the oscillator pitch knobs
    double pitch1 = 0, pitch2 = 0; int slot = 0;
    for (int i = 0; i < kNumRows; ++i) {
        const Row& r = P.rows[(size_t)i];
        if (r.src == SRC_OFF || r.dst == DST_OFF || std::abs(r.amount) < 1e-5) continue;
        if (r.src == SRC_CONSTANT && (r.dst == DST_OSC1_PITCH || r.dst == DST_OSC2_PITCH || r.dst == DST_MAIN_PITCH)) {
            const double st = pitchModSemis(r.amount);
            if (r.dst != DST_OSC2_PITCH) pitch1 += st;
            if (r.dst != DST_OSC1_PITCH) pitch2 += st;
            continue;
        }
        const int nd = dstO2N(r.dst);
        if (nd == Mod::DST_OFF) continue;
        double amt = r.amount * 100.0 * srcFactor(r.src) * dstFactor(r.dst);
        if (r.dst == DST_RESONANCE) amt *= std::max(0.2, P.resonance);   // the original scales the knob, native adds
        set(s, Mod::slotSrcID(slot), (float)srcO2N(r.src));
        set(s, Mod::slotAmtID(slot), (float)juce::jlimit(-100.0, 100.0, amt));
        set(s, Mod::slotDstID(slot), (float)nd);
        if (++slot >= Mod::NUM_SLOTS) break;
    }
    set(s, PID::O1PIT, (float)juce::jlimit(-24.0, 24.0, pitch1));
    set(s, PID::O2PIT, (float)juce::jlimit(-24.0, 24.0, pitch2));
    return "TRANSFERRED TO NATIVE";
}

// ---- NATIVE -> ORIGINAL ----
inline juce::String nativeToOriginal(juce::AudioProcessorValueTreeState& s) {
    using namespace detail; using namespace AlphaOrig;
    Patch P;
    const juce::String W[2][2] = { { PID::O1WA, PID::O1WB }, { PID::O2WA, PID::O2WB } }, O[2][2] = { { PID::O1OCA, PID::O1OCB }, { PID::O2OCA, PID::O2OCB } };
    for (int o = 0; o < 2; ++o) for (int ab = 0; ab < 2; ++ab) {
        P.wave[o][ab] = nativeToOrigWave(juce::roundToInt(get(s, W[o][ab])));
        P.range[o][ab] = juce::jlimit(-2, 2, juce::roundToInt(get(s, O[o][ab])) - 2);
    }
    P.mix[0] = get(s, PID::O1MRP); P.mix[1] = get(s, PID::O2MRP); P.balance = get(s, PID::MIX); P.drive = get(s, PID::DRV);
    P.detune = juce::jlimit(0.0, 100.0, std::abs(get(s, PID::O1DET) - get(s, PID::O2DET)) * 0.5 / 4.015);
    P.spread = juce::jlimit(0.0, 50.0, get(s, PID::UNI) * 30.0 / 1.73);
    P.filterType = juce::jlimit(0, 4, juce::roundToInt(get(s, PID::FTYP)));
    P.cutoff = hzToOrigKnob(get(s, PID::FCUT), P.filterType);
    P.resonance = get(s, PID::FRES);
    const double u0 = knobToU(P.cutoff), nd = get(s, PID::FDEP);
    P.envDepth = juce::jlimit(-1.0, 1.0, nd >= 0 ? nd / std::max(0.05, 2.0 * (1.0 - u0)) : nd / std::max(0.05, 2.0 * u0));
    auto env = [&](EnvParams& e, const juce::String& a, const juce::String& d, const juce::String& su, const juce::String& r, const juce::String& f) {
        e.attack = juce::jlimit(0.001, 10.0, get(s, a) / kAtt); e.decay = juce::jlimit(0.001, 10.0, get(s, d) / kDec);
        e.sustain = get(s, su); e.release = juce::jlimit(0.001, 10.0, get(s, r) / kDec); e.fade = nativeFadeToOrig(get(s, f), e.sustain);
    };
    env(P.ampEnv, PID::AATT, PID::ADEC, PID::ASUS, PID::AREL, PID::AFAD);
    env(P.filEnv, PID::FATT, PID::FDEC, PID::FSUS, PID::FREL, PID::FFAD);
    const double vol = get(s, PID::AVOL);
    P.volume = vol <= 0.0005 ? 0.0 : juce::jlimit(0.0, 1.0, (20.0 * std::log10(vol / kVolRef) / 0.2 + 91.0) / 181.0);
    P.velSens = get(s, PID::AVEL);
    const double nw = get(s, PID::HWET);
    P.chorusWet = juce::jlimit(0.0, 1.0, nw / std::max(0.02, 2.0 * (1.0 - nw)));
    P.chorusMs = juce::jlimit(0.01, 6.0, (get(s, PID::HTIM) - 0.18) / 1.2);
    P.chorusHz = juce::jlimit(0.02, 4.0, (double)get(s, PID::HRAT));
    const int vm = juce::roundToInt(get(s, PID::VMODE)); const double gl = get(s, PID::GLID);
    P.voices = vm == 0 ? 0 : 1;
    P.glideMode = vm == 2 ? 1 : (gl > 0.001 ? 2 : 0);
    P.glideTime = juce::jlimit(0.001, 4.0, std::max(0.001, gl));
    P.lfoWave = juce::jlimit(0, 5, juce::roundToInt(get(s, Mod::lfoWaveID(0)))); P.lfoHz = juce::jlimit(0.01, 32.0, (double)get(s, Mod::lfoRateID(0)));
    P.lfoSync = juce::jlimit(0, kNumSync - 1, juce::roundToInt(get(s, Mod::lfoSyncID(0)))); P.lfoAttack = juce::jlimit(0.0, 10.0, (double)get(s, Mod::lfoAttID(0)));
    // matrix (rows the original cannot use are left out); pitch knobs become constant rows
    int row = 0;
    auto addRow = [&](int src, double amt, int dst) {
        if (row >= kNumRows) return;
        P.rows[(size_t)row++] = { src, dst, juce::jlimit(-1.0, 1.0, amt) };
    };
    for (int i = 0; i < Mod::NUM_SLOTS; ++i) {
        const int ns = juce::roundToInt(get(s, Mod::slotSrcID(i))), ndst = juce::roundToInt(get(s, Mod::slotDstID(i)));
        const double a = get(s, Mod::slotAmtID(i)) * 0.01;
        if (ns == Mod::SRC_OFF || ndst == Mod::DST_OFF || std::abs(a) < 1e-5) continue;
        int os = -1, od = -1;
        for (int k = 1; k < NUM_SRC; ++k) if (srcO2N(k) == ns) { os = k; break; }
        for (int k = 1; k < NUM_DST; ++k) if (dstO2N(k) == ndst) { od = k; break; }
        if (os < 0 || od < 0) continue;
        double amt = a / (srcFactor(os) * dstFactor(od));
        if (od == DST_RESONANCE) amt /= std::max(0.2, P.resonance);
        addRow(os, amt, od);
    }
    const double p1 = get(s, PID::O1PIT), p2 = get(s, PID::O2PIT);
    if (std::abs(p1) > 0.005) addRow(SRC_CONSTANT, invPitchMod(p1), DST_OSC1_PITCH);
    if (std::abs(p2) > 0.005) addRow(SRC_CONSTANT, invPitchMod(p2), DST_OSC2_PITCH);
    OrigP::applyPatch(s, P);
    return "TRANSFERRED TO ORIGINAL";
}

// Snapshot / restore one engine's parameters (used when a randomizer of one
// engine is borrowed for the other)
struct Snapshot {
    std::vector<std::pair<juce::RangedAudioParameter*, float>> v;
    void take(juce::AudioProcessorValueTreeState& s, bool originalParams) {
        v.clear();
        for (auto* prm : s.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(prm)) {
                const auto id = rp->getParameterID();
                if (id == OrigP::ENGINE || OrigP::isOrigID(id) != originalParams) continue;
                v.push_back({ rp, rp->getValue() });
            }
    }
    void restore() { for (auto& [p, x] : v) p->setValueNotifyingHost(x); }
};
}
