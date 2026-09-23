#pragma once
//==============================================================================
//  VirusOscModule.h
//
//  An Aquanode module written for this synth, in the same shape as the rest
//  of the set: it registers with the factory, speaks the same socket and
//  parameter interface, and could be dropped into Aquanode Modular as-is.
//
//  It exists because the Virus oscillator does four things no module in the
//  set does:
//
//    - a wave table of 64 waveforms rather than four fixed shapes
//    - pulse width, with Shape morphing wave -> saw -> pulse
//    - hard sync, including the Virus's separate sync frequency
//    - formant shift, which scans a cycle faster than it repeats
//
//  All three Virus oscillator models live here under one Mode parameter,
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
// the Virus uses and the reason a patch's wave number still lands somewhere
// recognisable even though the spectra are ours rather than Access's.
//==============================================================================
class VirusWaveTable
{
public:
    static constexpr int kNumWaves = 64;
    static constexpr int kTableSize = 1024;
    static constexpr int kNumMips = 8;   // 256, 128, 64 ... 2 harmonics

    static const VirusWaveTable& instance()
    {
        static VirusWaveTable table;
        return table;
    }

    // phase 0..1, wave 0..63, frequency for choosing the band limit
    float read (int wave, double phase01, double frequencyHz, double sampleRate) const
    {
        const int mip = mipFor (frequencyHz, sampleRate);
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

private:
    VirusWaveTable()
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
class VirusOscModule : public aquanode::SynthModule
{
public:
    static constexpr int kMaxUnison = 9;

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void prepare (double newSampleRate) override
    {
        aquanode::SynthModule::prepare (newSampleRate);
        VirusWaveTable::instance();   // build the tables off the audio thread
        reset();
    }

    // Fixed seeds: the "random" start phases and drift are the same sequence
    // after every reset, so a render is repeatable (tests, bouncing a track
    // twice) while notes within a performance still all differ.
    void reset() override
    {
        phaseSeed = 0x2545F491u;
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
                         float formant, double frequency);

    aquanode::ModuleVoicePool pool;
    aquanode::ModuleGlide glide;

    double phase [aquanode::kMaxVoices][kMaxUnison] {};
    float  drift [aquanode::kMaxVoices][kMaxUnison] {};
    double driftCount [aquanode::kMaxVoices][kMaxUnison] {};
    double syncPhase [aquanode::kMaxVoices] {};
    double freqHz [aquanode::kMaxVoices] {};
    float  gateLvl [aquanode::kMaxVoices] {};
    bool   gateOn [aquanode::kMaxVoices] {};
    float  vel [aquanode::kMaxVoices] {};
    bool   lastSyncHigh [aquanode::kMaxVoices] {};

    juce::Random rng;
    juce::uint32 phaseSeed { 0x2545F491u };

    int pVolume { -1 }, pRatio { -1 }, pMode { -1 }, pWave { -1 }, pShape { -1 },
        pPulseWidth { -1 }, pUnison { -1 }, pDetune { -1 }, pSpread { -1 },
        pDrift { -1 }, pSync { -1 }, pSyncRatio { -1 }, pFormant { -1 }, pSyncOut { -1 },
        pInitPhase { -1 }, pSyncEnv { -1 };

    void resolveIndices();
    bool indicesResolved { false };
};

} // namespace aquavibrio
