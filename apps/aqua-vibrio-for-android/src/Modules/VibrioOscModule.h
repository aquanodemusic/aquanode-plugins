#pragma once
//==============================================================================
//  VibrioOscModule.h
//
//  An Aquanode module written for this synth, in the same shape as the rest
//  of the set: it registers with the factory, speaks the same socket and
//  parameter interface, and could be dropped into Aquanode Modular as-is.
//
//  It exists because the hardware oscillator does four things no module in the
//  set does:
//
//    - a wave table of 64 waveforms rather than four fixed shapes
//    - pulse width, with Shape morphing wave -> saw -> pulse
//    - hard sync, including the hardware's separate sync frequency
//    - formant shift, which scans a cycle faster than it repeats
//
//  All three hardware oscillator models live here under one Mode parameter,
//  exactly as they do on the hardware, so switching model is a parameter
//  change rather than a different module.
//
//  Sockets: 0 = FM In, 1 = Sync In, 2 = Env In, out 0 = Audio Out.
//  Sync Out is the master's cycle wrap, sent on the output's right channel
//  when syncOut is on, which is what the slave's Sync In wants.
//==============================================================================

#include <JuceHeader.h>
#include "../Aquanode/ModuleCore.h"

namespace aquavibrio
{

//==============================================================================
// The wave table: 64 single-cycle waveforms, generated additively and stored
// at eight band limits so a high note does not alias. Wave 1 is a sine and
// wave 2 a triangle, matching the hardware's first two slots; the rest run
// from hollow and narrow through to bright and buzzy, which is the ordering
// the hardware uses and the reason a patch's wave number still lands somewhere
// recognisable even though the spectra are ours rather than the hardware's.
//==============================================================================
class VibrioWaveTable
{
public:
    static constexpr int kNumWaves = 64;
    static constexpr int kTableSize = 1024;
    static constexpr int kNumMips = 8;   // 256, 128, 64 ... 2 harmonics

    static const VibrioWaveTable& instance()
    {
        static VibrioWaveTable table;
        return table;
    }

    // phase 0..1, wave 0..63, frequency for choosing the band limit
    float read (int wave, double phase01, double frequencyHz, double sampleRate) const
    {
        return readMip (wave, phase01, mipFor (frequencyHz, sampleRate));
    }

    // The same, with the band-limit level already chosen (the oscillator
    // works it out once per partial rather than once per table read).
    float readMip (int wave, double phase01, int mip) const
    {
        const auto* data = tables[(size_t) juce::jlimit (0, kNumWaves - 1, wave)][(size_t) mip].data();

        const double pos = phase01 * kTableSize;
        const int i0 = (int) pos & (kTableSize - 1);
        const int i1 = (i0 + 1) & (kTableSize - 1);
        const float frac = (float) (pos - std::floor (pos));

        return data[i0] * (1.0f - frac) + data[i1] * frac;
    }

    static int mipFor (double frequencyHz, double sampleRate)
    {
        const double nyquist = sampleRate * 0.5;
        const double maxHarmonics = nyquist / juce::jmax (1.0, frequencyHz);

        for (int m = 0; m < kNumMips; ++m)
            if ((double) harmonicsAtMip (m) <= maxHarmonics)
                return m;

        return kNumMips - 1;
    }

    static int harmonicsAtMip (int mip) { return 256 >> mip; }

    // A table built from sampled single cycles instead of the recipes below:
    // waves 0 and 1 stay the generated sine and triangle (as on the hardware),
    // cycles[k] becomes wave k + 2. Each cycle is analysed into harmonics and
    // rebuilt at every band limit, so it plays alias-free like the others.
    // Used by the Original engine for the sampled hardware waveforms; the native
    // engine never builds one.
    // perWaveLevels: each cycle normalised to its own peak (single-cycle
    // wave sets); otherwise one normalisation for the whole set, which keeps
    // the frames of a wavetable at their recorded relative levels.
    explicit VibrioWaveTable (const std::vector<std::vector<float>>& cycles, bool perWaveLevels = true)
    {
        float commonPeak = 0.0f;
        for (int w = 0; w < kNumWaves; ++w)
        {
            const int k = w - 2;
            if (k < 0 || k >= (int) cycles.size() || cycles[(size_t) k].size() < 16)
            {
                for (int m = 0; m < kNumMips; ++m)
                    build (w, m);
                continue;
            }

            // DFT of the cycle, harmonics 1..256
            const auto& c = cycles[(size_t) k];
            const int n = (int) c.size();
            std::array<double, 257> re {}, im {};
            for (int h = 1; h <= 256 && h < n / 2; ++h)
            {
                double sr = 0.0, si = 0.0;
                for (int i = 0; i < n; ++i)
                {
                    const double ph = juce::MathConstants<double>::twoPi * (double) h * (double) i / (double) n;
                    sr += (double) c[(size_t) i] * std::cos (ph);
                    si += (double) c[(size_t) i] * std::sin (ph);
                }
                re[(size_t) h] = sr; im[(size_t) h] = si;
            }

            float peak = 0.0f;
            for (int m = 0; m < kNumMips; ++m)
            {
                auto& t = tables[(size_t) w][(size_t) m];
                const int maxH = juce::jmin (256, harmonicsAtMip (m));
                for (int i = 0; i < kTableSize; ++i)
                {
                    double sum = 0.0;
                    const double ph = juce::MathConstants<double>::twoPi * (double) i / (double) kTableSize;
                    for (int h = 1; h <= maxH; ++h)
                        sum += re[(size_t) h] * std::cos (ph * h) + im[(size_t) h] * std::sin (ph * h);
                    t[(size_t) i] = (float) sum;
                    if (m == 0) peak = juce::jmax (peak, (float) std::abs (sum));
                }
            }
            // one normalisation for every band limit, so levels match across the keyboard
            if (perWaveLevels && peak > 0.0f)
                for (auto& t : tables[(size_t) w])
                    for (auto& s : t)
                        s /= peak;
            commonPeak = juce::jmax (commonPeak, peak);
        }

        if (! perWaveLevels && commonPeak > 0.0f)
            for (int w = 2; w < kNumWaves && w - 2 < (int) cycles.size(); ++w)
                for (auto& t : tables[(size_t) w])
                    for (auto& s : t)
                        s /= commonPeak;
    }

private:
    VibrioWaveTable()
    {
        for (int w = 0; w < kNumWaves; ++w)
            for (int m = 0; m < kNumMips; ++m)
                build (w, m);
    }

    // Each wave is a recipe for harmonic amplitudes. Wave 0 is a sine, wave 1
    // a triangle, and from there the amplitude of harmonic n follows a rule
    // that varies with the wave number: a tilt that gets brighter, a comb
    // that notches out parts of the spectrum, and an odd/even balance that
    // slides from hollow to full.
    static float harmonicAmplitude (int wave, int harmonic)
    {
        if (harmonic < 1)
            return 0.0f;

        if (wave == 0)
            return harmonic == 1 ? 1.0f : 0.0f;                 // sine

        if (wave == 1)
            return harmonic % 2 == 1                            // triangle
                     ? 1.0f / (float) (harmonic * harmonic) * ((harmonic % 4 == 1) ? 1.0f : -1.0f)
                     : 0.0f;

        const float w = (float) (wave - 2) / (float) (kNumWaves - 3);   // 0..1
        const float n = (float) harmonic;

        // brightness: low waves roll off fast, high waves barely at all
        const float tilt = std::pow (n, -(2.2f - 1.7f * w));

        // odd/even balance: hollow (odd only) at the bottom, full at the top
        const float oddOnly = harmonic % 2 == 1 ? 1.0f : w * w;

        // a slow comb across the spectrum, different for every wave, which is
        // what stops the table sounding like one filter sweep
        const float comb = 0.55f + 0.45f * std::cos (n * (0.6f + 2.4f * w));

        return tilt * oddOnly * comb;
    }

    void build (int wave, int mip)
    {
        auto& table = tables[(size_t) wave][(size_t) mip];
        table.fill (0.0f);

        const int maxHarmonic = harmonicsAtMip (mip);
        float peak = 0.0f;

        for (int i = 0; i < kTableSize; ++i)
        {
            const double phase = juce::MathConstants<double>::twoPi * (double) i / (double) kTableSize;
            float sum = 0.0f;

            for (int h = 1; h <= maxHarmonic; ++h)
            {
                const float amp = harmonicAmplitude (wave, h);

                if (amp != 0.0f)
                    sum += amp * (float) std::sin (phase * h);
            }

            table[(size_t) i] = sum;
            peak = juce::jmax (peak, std::abs (sum));
        }

        if (peak > 0.0f)
            for (auto& s : table)
                s /= peak;
    }

    std::array<std::array<std::array<float, kTableSize>, kNumMips>, kNumWaves> tables {};
};

//==============================================================================
class VibrioOscModule : public aquanode::SynthModule
{
public:
    static constexpr int kMaxUnison = 9;

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void prepare (double newSampleRate) override
    {
        aquanode::SynthModule::prepare (newSampleRate);
        VibrioWaveTable::instance();   // build the tables off the audio thread
        reset();

        // the band-limit level depends on the sample rate
        for (auto& voice : mips)
            for (auto& m : voice)
                m.clear();
    }

    // Fixed seeds: the "random" start phases and drift are the same sequence
    // after every reset, so a render is repeatable (tests, bouncing a track
    // twice) while notes within a performance still all differ.
    void reset() override
    {
        phaseSeed = seedBase;
        rng.setSeed (0x5eed);
    }

    void processVoiceSample (int voice, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;

    void voiceNoteOn (int voice, int note, bool retrigger) override;
    void voiceVelocity (int voice, float velocity01) override { vel[(size_t) voice] = velocity01; }
    void voiceNoteOff (int voice) override;
    void voiceReset (int voice) override;
    double voiceTailSeconds() const override { return 0.02; }

private:
    float renderPartial (float wave, double phase, float shape, float pulseWidth,
                         float formant, double frequency, int mip, float waveGain, float pulseGain);

    std::atomic<const VibrioWaveTable*> table { &VibrioWaveTable::instance() };

public:
    // Swap in another wave table (nullptr = the built-in one). The table must
    // outlive the module; the Original engine keeps every one it loads.
    void setWaveTable (const VibrioWaveTable* t) { table.store (t != nullptr ? t : &VibrioWaveTable::instance()); }

    // A different seed for the free-running start phases, so two oscillators
    // do not draw the same "random" phases (and sum coherently). Opt-in.
    void setPhaseSeed (juce::uint32 seed) { seedBase = phaseSeed = seed; }

    // Levels of the wave and the pulse against the saw inside the Classic
    // Shape crossfade. Opt-in (the Original engine's measured levels: the
    // wave and the pulse enter the crossfade louder than the saw, so the saw
    // half of a part-way morph is not lifted with them). 1, 1 = unchanged.
    void setMorphGains (float wave, float pulse) { waveGain = wave; pulseGain = pulse; }

    // The narrowest pulse allowed (2 % by default). The Original engine opts
    // down to 0, where the pulse vanishes as it does in the reference.
    void setNarrowestPulse (float width) { narrowestPulse = juce::jlimit (0.0f, 0.5f, width); }

    // Through-zero FM (opt-in, the Original engine): FM In may push the
    // frequency below zero, where the cycle runs backwards, instead of
    // stopping at a few hundredths of a hertz.
    void setThroughZeroFm (bool on) { throughZero = on; }

    // Band-limited hard sync (opt-in, the Original engine). Every step in the
    // waveform - the cycle's own wrap, the pulse edge and a sync reset - is
    // placed at its exact time between two samples and smoothed with a
    // two-sided PolyBLEP, which costs one sample of delay. The master's cycle
    // runs at the unmodulated pitch, so FM changes the slave's shape and not
    // the moments it is reset. Off, a reset lands on the next whole sample at
    // phase zero, and FM moves the master too (the old behaviour).
    void setBandLimitedSync (bool on) { bandLimitedSync = on; }

    // Opt-in: a fresh note starts at full level instead of ramping in over
    // the anti-click 3 ms. Off by default.
    void setInstantGate (bool on) { instantGate = on; }

    // A voice's phase (partial 0), and the phase of its sync master: the
    // Original engine starts oscillator 2's master where oscillator 1 is, so
    // the resets fall on oscillator 1's own cycle, as on the hardware.
    double partialPhase (int voice) const { return phase[voice][0]; }

    // An extra fixed detune per voice and partial, in cents (opt-in; the
    // Original engine gives each copy of a unison stack its own, as the
    // reference does with its one-voice-per-copy unison).
    void setPartialCents (int voice, int partial, float cents) { partialCents[voice][partial] = cents; }
    void setSyncPhase (int voice, double p) { syncPhase[voice] = p - std::floor (p); }

private:
    aquanode::ModuleVoicePool pool;
    aquanode::ModuleGlide glide;

    double phase [aquanode::kMaxVoices][kMaxUnison] {};
    float  drift [aquanode::kMaxVoices][kMaxUnison] {};
    double driftCount [aquanode::kMaxVoices][kMaxUnison] {};
    double syncPhase [aquanode::kMaxVoices] {};
    double freqHz [aquanode::kMaxVoices] {};

    // per-sample conversions whose inputs rarely move
    aquanode::Memo<float, double> noteHz [aquanode::kMaxVoices];
    aquanode::Memo<double, double> centsRatio [aquanode::kMaxVoices][kMaxUnison];
    aquanode::Memo<float, std::pair<float, float>> panGains [aquanode::kMaxVoices][kMaxUnison];
    aquanode::Memo<double, int> mips [aquanode::kMaxVoices][kMaxUnison];
    float  gateLvl [aquanode::kMaxVoices] {};
    bool   gateOn [aquanode::kMaxVoices] {};
    float  vel [aquanode::kMaxVoices] {};
    bool   lastSyncHigh [aquanode::kMaxVoices] {};

    juce::Random rng;
    float waveGain { 1.0f }, pulseGain { 1.0f }, narrowestPulse { 0.02f };
    bool throughZero { false };
    bool bandLimitedSync { false }, instantGate { false };
    float held [aquanode::kMaxVoices][kMaxUnison] {};
    float partialCents [aquanode::kMaxVoices][kMaxUnison] {};   // band-limited path: the sample still taking corrections

    float naiveValue (float wave, double p, float shape, float pulseWidth,
                      float formant, int mip, float waveGain, float pulseGain) const;
    float renderBandLimited (int voice, int partial, double freq, bool reset, double sinceWrap,
                             float wave, float shape, float pulseWidth, float formant,
                             float waveGainNow, float pulseGainNow);
    juce::uint32 phaseSeed { 0x2545F491u };
    juce::uint32 seedBase { 0x2545F491u };   // what reset() returns to

    int pVolume { -1 }, pRatio { -1 }, pMode { -1 }, pWave { -1 }, pShape { -1 },
        pPulseWidth { -1 }, pUnison { -1 }, pDetune { -1 }, pSpread { -1 }, pPanMode { -1 },
        pDrift { -1 }, pSync { -1 }, pSyncRatio { -1 }, pFormant { -1 }, pSyncOut { -1 },
        pInitPhase { -1 }, pSyncEnv { -1 };

    void resolveIndices();
    bool indicesResolved { false };
};

} // namespace aquavibrio
