#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <cstdint>

// ==============================================================
//  Modulation Matrix – sources, destinations, LFO helpers
//  Modelled on Appendix D of the Alpha 3 manual.
// ==============================================================
namespace Mod {

    constexpr int NUM_SLOTS = 11;   // Alpha 3.3 shows 11 visible slots
    constexpr int NUM_LFOS = 3;
    constexpr int CTRL_RATE = 16;   // samples per modulation update

    enum Source {
        SRC_OFF = 0, SRC_NOTE_LOG, SRC_NOTE_LIN, SRC_VELOCITY, SRC_AFTERTOUCH,
        SRC_PITCHWHEEL, SRC_MODWHEEL, SRC_BREATH, SRC_FOOT, SRC_EXPRESSION,
        SRC_CC16, SRC_CC17, SRC_CC18, SRC_CC19,
        SRC_AMP_ENV, SRC_FILT_ENV, SRC_LFO1, SRC_LFO2, SRC_LFO3, SRC_CONSTANT,
        NUM_SOURCES
    };

    enum Dest {
        DST_OFF = 0, DST_OSC1_AMP, DST_OSC1_PITCH, DST_OSC1_SYM,
        DST_OSC2_AMP, DST_OSC2_PITCH, DST_OSC2_SYM, DST_OSC2_RING,
        DST_NOISE_AMP, DST_CUTOFF, DST_CUTOFF_FM, DST_RESONANCE,
        DST_MAIN_AMP, DST_MAIN_PITCH,
        DST_MDEPTH1, DST_MDEPTH2, DST_MDEPTH3,
        DST_LFO1_SPEED, DST_LFO2_SPEED,
        // added in 1.3 - appended so older presets keep their routings
        DST_UNISON, DST_OSC1_WTPOS, DST_OSC2_WTPOS,
        NUM_DESTS
    };

    // 16-character CRT names, like the original display
    inline const juce::StringArray& sourceNames() {
        static const juce::StringArray s{
            "- - -", "Note played(log)", "Note played(lin)", "Velocity", "Aftertouch",
            "Pitch Wheel", "Modulation Wheel", "Breath Contr.", "Foot Controller",
            "Expression Contr", "CC16 Controller", "CC17 Controller", "CC18 Controller",
            "CC19 Controller", "Amp Envelope", "Filter Envelope", "LFO 1", "LFO 2", "LFO 3",
            "Constant" };
        return s;
    }

    inline const juce::StringArray& destNames() {
        static const juce::StringArray d{
            "- - -", "Osc 1 Amplitude", "Osc 1 Pitch", "Osc 1 Symmetry",
            "Osc 2 Amplitude", "Osc 2 Pitch", "Osc 2 Symmetry", "Osc 2 Ringmod",
            "Noise Amplitude", "Filter Cutoff", "Filter Cutoff FM", "Filter Resonance",
            "Main Amplitude", "Main Pitch",
            "Matrix Depth 1", "Matrix Depth 2", "Matrix Depth 3",
            "LFO 1 Speed", "LFO 2 Speed",
            "Unison Detune", "Osc 1 WT Pos", "Osc 2 WT Pos" };
        return d;
    }

    inline bool isPitchDest(int d) {
        return d == DST_OSC1_PITCH || d == DST_OSC2_PITCH || d == DST_MAIN_PITCH;
    }

    // Scaling of a 100% matrix amount at each destination
    constexpr float PITCH_SEMIS_FULL = 24.0f;   // 100% = two octaves
    constexpr float CUTOFF_OCT_FULL = 5.0f;     // 100% = five octaves
    constexpr float LFO_SPEED_OCT_FULL = 4.0f;  // 100% = x16 speed

    // ---- Parameter IDs ----
    inline juce::String slotSrcID(int i) { return "mm" + juce::String(i + 1) + "_src"; }
    inline juce::String slotAmtID(int i) { return "mm" + juce::String(i + 1) + "_amt"; }
    inline juce::String slotDstID(int i) { return "mm" + juce::String(i + 1) + "_dst"; }

    inline juce::String lfoWaveID(int k) { return "lfo" + juce::String(k + 1) + "_wave"; }
    inline juce::String lfoRateID(int k) { return "lfo" + juce::String(k + 1) + "_rate"; }
    inline juce::String lfoSyncID(int k) { return "lfo" + juce::String(k + 1) + "_sync"; }
    inline juce::String lfoAttID(int k) { return "lfo" + juce::String(k + 1) + "_att"; }
    inline juce::String lfoModeID(int k) { return "lfo" + juce::String(k + 1) + "_mode"; }

    // ==========================================================
    //  LFO
    // ==========================================================
    enum LfoWave { LFO_SINE = 0, LFO_TRI, LFO_SAW, LFO_SQUARE, LFO_NOISE, LFO_SAMHO };

    inline const juce::StringArray& lfoWaveNames() {
        static const juce::StringArray w{ "Sine", "Triangle", "Sawtooth", "Square", "Noise", "SamHo" };
        return w;
    }

    // Appendix C of the manual. "*" = dotted, "T" = triplet.
    inline const juce::StringArray& syncNames() {
        static const juce::StringArray s{
            "Off", "16/1*", "16/1", "16/1T", "8/1*", "8/1", "8/1T", "4/1*", "4/1", "4/1T",
            "2/1*", "2/1", "2/1T", "1/1*", "1/1", "1/1T", "1/2*", "1/2", "1/2T",
            "1/4*", "1/4", "1/4T", "1/8*", "1/8", "1/8T", "1/16*", "1/16", "1/16T",
            "1/32*", "1/32", "1/32T", "1/64", "5/16", "7/16", "9/16", "5/8", "11/16",
            "13/16", "7/8", "15/16" };
        return s;
    }

    // Length of one LFO cycle in quarter-note beats (0 = not synced)
    inline double syncBeats(int index) {
        if (index <= 0 || index >= syncNames().size()) return 0.0;
        auto name = syncNames()[index];
        double mult = 1.0;
        if (name.endsWithChar('*')) { mult = 1.5;        name = name.dropLastCharacters(1); }
        else if (name.endsWithChar('T')) { mult = 2.0 / 3.0; name = name.dropLastCharacters(1); }
        auto num = name.upToFirstOccurrenceOf("/", false, false).getDoubleValue();
        auto den = name.fromFirstOccurrenceOf("/", false, false).getDoubleValue();
        if (den <= 0.0) return 0.0;
        return 4.0 * (num / den) * mult;   // whole note = 4 beats
    }

    // Deterministic hash → [-1, 1]. Lets mono LFOs share random values
    // across voices without any shared mutable state.
    inline float hashRandom(uint64_t x) {
        x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
        x ^= x >> 33;
        return (float)((double)(x >> 11) * (1.0 / 9007199254740992.0)) * 2.0f - 1.0f;
    }

    // phase in cycles (unwrapped, so the integer part identifies the cycle)
    inline float lfoShape(int wave, double phase, uint64_t seed) {
        double fl = std::floor(phase);
        float p = (float)(phase - fl);
        auto cycle = (uint64_t)(int64_t)fl;
        switch (wave) {
        case LFO_SINE:   return std::sin(juce::MathConstants<float>::twoPi * p);
        case LFO_TRI:    return p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
        case LFO_SAW:    return 2.0f * p - 1.0f;
        case LFO_SQUARE: return p < 0.5f ? 1.0f : -1.0f;
        case LFO_NOISE: {   // smooth random: cosine-interpolated between cycle points
            float a = hashRandom(seed * 0x9E3779B97F4A7C15ULL + cycle);
            float b = hashRandom(seed * 0x9E3779B97F4A7C15ULL + cycle + 1);
            float t = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * p);
            return a + (b - a) * t;
        }
        case LFO_SAMHO:  return hashRandom(seed * 0x9E3779B97F4A7C15ULL + cycle);
        }
        return 0.0f;
    }

    // ==========================================================
    //  Shared context the processor fills once per block and every
    //  voice reads. Written on the audio thread only.
    // ==========================================================
    struct MidiState {
        float pitchWheel = 0.0f;               // -1..1
        float channelPressure = 0.0f;          // 0..1
        std::array<float, 128> cc{};           // 0..1
    };

    struct Slot { int src = SRC_OFF; float amt = 0.0f; int dst = DST_OFF; };

    struct LfoParams {
        int   wave = LFO_TRI;
        float rateHz = 1.0f;
        double syncBeats = 0.0;                // > 0 → tempo synced
        float attack = 0.0f;                   // seconds
        bool  poly = false;
    };

    struct Context {
        MidiState midi;
        std::array<Slot, NUM_SLOTS> slots;
        std::array<LfoParams, NUM_LFOS> lfo;
        // Mono LFOs: phase at block start + per-sample increment.
        std::array<double, NUM_LFOS> monoPhase0{};
        std::array<double, NUM_LFOS> monoInc{};
        float bendRange = 2.0f;                // semitones
        double bpm = 120.0;
    };

    // Effective LFO rate in Hz
    inline double lfoRateHz(const LfoParams& p, double bpm) {
        if (p.syncBeats > 0.0) return (bpm / 60.0) / p.syncBeats;
        return p.rateHz;
    }
}
