#pragma once
#include "Wavetable.h"
#include <functional>

// ==============================================================
//  Internal waves
//
//  Alpha 3 has 30 waveforms. AlphaBeta keeps its own Sine / Tri /
//  Saw / Square (= Alpha's Square1) and rebuilds the other 26 by
//  name. The originals aren't available, so these are new designs
//  in the spirit of each name, built additively and stored as
//  band-limited single-frame wavetables.
//
//  Wave choice indices: 0 Sine, 1 Tri, 2 Saw, 3 Square, 4 Noise,
//  5 Table (user wavetable), 6.. internal waves below.
// ==============================================================
namespace InternalWaves {

    constexpr int FIRST_INDEX = 6;

    inline const juce::StringArray& names() {
        static const juce::StringArray n{
            "Square2", "Square3",
            "Organ1", "Organ2", "Organ3",
            "Spectra1", "Spectra2", "Spectra3", "Spectra4",
            "RichSaw1", "RichSaw2", "RichSaw3", "RichSaw4",
            "SawSpec1", "SawSpec2",
            "VintSaw1", "VintSaw2", "VintSaw3",
            "SawBass1", "SawBass2", "SawBass3", "SawBass4",
            "SawBass5", "SawBass6", "SawBass7", "SawBass8" };
        return n;
    }

    // Full list for the wave choice parameter
    inline juce::StringArray allWaveNames() {
        juce::StringArray w{ "Sine", "Tri", "Saw", "Square", "Noise", "Table" };
        w.addArray(names());
        return w;
    }

    namespace detail {
        constexpr int H = Wavetable::N / 2;   // harmonics 1..1023
        inline float gauss(float k, float centre, float width) {
            float d = (k - centre) / width;
            return std::exp(-d * d);
        }
        // 2-pole low-pass response at harmonic k (cutoff fc, resonance q)
        inline float lp2(float k, float fc, float q) {
            float r = k / fc;
            return 1.0f / std::sqrt((1.0f - r * r) * (1.0f - r * r) + (r / q) * (r / q));
        }
        // Builds a frame from an amplitude function and optional phase offsets
        inline std::vector<float> build(const std::function<float(int)>& amp,
                                        float phaseJitter = 0.0f, int seed = 1) {
            std::vector<float> s((size_t)H, 0.0f), c((size_t)H, 0.0f);
            juce::Random rng(seed);
            for (int k = 1; k < H; ++k) {
                float a = amp(k);
                float ph = phaseJitter * (rng.nextFloat() * 2.0f - 1.0f);
                s[(size_t)k] = a * std::cos(ph);
                c[(size_t)k] = a * std::sin(ph);
            }
            return Wavetable::frameFromHarmonics(s, c);
        }
        inline std::vector<float> pulse(float duty) {
            std::vector<float> c((size_t)H, 0.0f);
            for (int k = 1; k < H; ++k)
                c[(size_t)k] = std::sin(juce::MathConstants<float>::pi * (float)k * duty) / (float)k;
            return Wavetable::frameFromHarmonics({}, c);
        }
        inline float saw(int k) { return 1.0f / (float)k; }
        inline float odd(int k) { return (k & 1) ? 1.0f / (float)k : 0.0f; }
    }

    inline std::vector<float> design(int i) {
        using namespace detail;
        switch (i) {
        // ---- Squares: narrower pulses ----
        case 0: return pulse(0.25f);                                         // Square2
        case 1: return pulse(0.10f);                                         // Square3
        // ---- Organs: drawbar mixes ----
        case 2: return build([](int k) {                                     // Organ1 soft jazz
            switch (k) { case 1: return 1.0f; case 2: return 0.7f; case 3: return 0.5f; case 4: return 0.15f; default: return 0.0f; } });
        case 3: return build([](int k) {                                     // Organ2 hollow
            switch (k) { case 1: return 1.0f; case 3: return 0.6f; case 5: return 0.35f; case 8: return 0.25f; default: return 0.0f; } });
        case 4: return build([](int k) {                                     // Organ3 full
            switch (k) { case 1: case 2: case 3: case 4: case 5: case 6: case 8: return 0.8f;
                         case 10: case 12: return 0.3f; default: return 0.0f; } });
        // ---- Spectra: shaped harmonic spectra ----
        case 5: return build([](int k) {                                     // Spectra1 formant
            return std::pow((float)k, -0.6f) * (0.25f + gauss((float)k, 6.0f, 2.5f)); });
        case 6: return build([](int k) {                                     // Spectra2 glassy
            return ((k & 1) ? 1.0f : 0.25f) * (0.15f / (float)k + gauss((float)k, 11.0f, 2.0f)); });
        case 7: return build([](int k) { return (k % 3 == 0) ? 0.0f : saw(k); });   // Spectra3 comb
        case 8: return build([](int k) {                                     // Spectra4 vocal
            return 0.3f / (float)k + gauss((float)k, 3.0f, 1.2f) + 0.6f * gauss((float)k, 10.0f, 2.5f); });
        // ---- Rich saws ----
        case 9:  return build([](int k) { return saw(k) * ((k % 2 == 0) ? 1.8f : 1.0f); });        // RichSaw1 +octave
        case 10: return build([](int k) { return saw(k) * (1.0f + 0.8f * gauss((float)k, 4.0f, 1.5f)); }); // RichSaw2 warm
        case 11: return build([](int k) { return std::pow((float)k, -0.85f) * (k > 300 ? 300.0f / (float)k : 1.0f); }); // RichSaw3 bright
        case 12: return build([](int k) { return saw(k) + 0.6f * odd(k); });                       // RichSaw4 saw+square
        // ---- Saw spectra ----
        case 13: return build([](int k) { return saw(k) * ((k >= 5 && k <= 9) ? 0.15f : 1.0f); }); // SawSpec1 notch
        case 14: return build([](int k) { return saw(k) * (1.0f + 3.0f * gauss((float)k, 18.0f, 3.0f)); }); // SawSpec2 peak
        // ---- Vintage saws: rolled off, slightly drifted phases ----
        case 15: return build([](int k) { float r = (float)k / 40.0f; return saw(k) / (1.0f + r * r); });
        case 16: return build([](int k) { return saw(k) * ((k % 2 == 0) ? 0.8f : 1.0f) * (k == 2 ? 1.3f : 1.0f); }, 0.3f, 7);
        case 17: return build([](int k) { float r = (float)k / 15.0f; return saw(k) / std::sqrt(1.0f + r * r); }, 0.6f, 11);
        // ---- Saw basses: resonant low-passed saws, darker -> brighter ----
        // cutoff (in harmonics) and resonance both rise through the series
        default: {
            const int b = juce::jlimit(0, 7, i - 18);
            const float fc[] = { 2.5f, 3.5f, 5.0f, 7.0f, 10.0f, 14.0f, 20.0f, 28.0f };
            const float q[]  = { 0.8f, 1.0f, 1.3f, 1.6f, 1.9f, 2.2f, 2.6f, 3.0f };
            const float fcb = fc[b], qb = q[b];
            return build([=](int k) { return saw(k) * lp2((float)k, fcb, qb); });
        }
        }
    }

    // Built once (the processor constructor calls this before audio starts)
    inline const std::vector<Wavetable::Ptr>& tables() {
        static const std::vector<Wavetable::Ptr> t = [] {
            std::vector<Wavetable::Ptr> v;
            for (int i = 0; i < names().size(); ++i)
                v.push_back(Wavetable::fromFrames(names()[i], design(i)));
            return v;
        }();
        return t;
    }

    // Table for a wave choice index, or nullptr for the non-table waves
    inline const Wavetable* forWave(int waveIndex) {
        int i = waveIndex - FIRST_INDEX;
        if (i < 0 || i >= (int)tables().size()) return nullptr;
        return tables()[(size_t)i].get();
    }
}
