#pragma once

#include "ModuleCore.h"

// Vocoder - adapted from the Vocoder VST, reduced from 256 bands to fixed
// 8/16/32-band banks. The Mod In signal's per-band envelope shapes the same
// bands of the Carrier In signal: speech into Mod In + a bright saw into
// Carrier In = the classic robot voice; drums into Mod In make rhythmic pads.
// Bright tilts the band gains toward the highs for intelligibility.
// Inputs: 0 = Carrier In, 1 = Mod In. Output: 0 = Audio Out.
class VocoderModule : public aquanode::SynthModule
{
public:
    static constexpr int kMaxBands = 32;
    static constexpr int kOrder = 2;   // cascaded bandpasses per path
    // FORKED for Aqua Vibrio: the carrier and modulator banks were derived
    // from one set of numbers; they now take their own centre, spread and Q,
    // which is what the Virus's separate carrier and modulator controls are
    // for. Upstream Aquanode has this module without the last six parameters.
    enum ParamIndex { pBands = 0, pAttack, pRelease, pBright, pLevel,
                      pCarrierCentre, pModCentre, pCarrierSpread, pModSpread,
                      pCarrierQ, pModQ };

    // transposed direct form II bandpass biquad (from the original)
    struct Biquad
    {
        float b0 {}, b1 {}, b2 {}, a1 {}, a2 {};
        float z1 {}, z2 {};

        inline float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void setBandpass (float fc, float q, float sr) noexcept
        {
            fc = juce::jlimit (20.0f, sr * 0.49f, fc);
            q = juce::jmax (0.05f, q);
            const float w0 = juce::MathConstants<float>::twoPi * fc / sr;
            const float alpha = std::sin (w0) / (2.0f * q);
            const float inv = 1.0f / (1.0f + alpha);
            b0 = alpha * inv;
            b1 = 0.0f;
            b2 = -alpha * inv;
            a1 = (-2.0f * std::cos (w0)) * inv;
            a2 = (1.0f - alpha) * inv;
        }

        void reset() noexcept { z1 = z2 = 0.0f; }
    };

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        dirty.store (true, std::memory_order_relaxed);
        reset();
    }

    void reset() override
    {
        for (int b = 0; b < kMaxBands; ++b)
        {
            for (int o = 0; o < kOrder; ++o)
            {
                modFilters[b][o].reset();
                carFiltersL[b][o].reset();
                carFiltersR[b][o].reset();
            }
            envelope[b] = 0.0f;
        }
    }

    // Any parameter that shapes the filter banks triggers a rebuild - but only
    // when its value actually changes. (Rebuilding on every write, as a host
    // or engine setting parameters each block does, restarted every filter
    // and envelope every few milliseconds and silenced the vocoder.)
    void setParameter (const juce::String& id, float value) override
    {
        const bool layout = id == "bands" || id == "carrierCentre" || id == "modCentre"
                         || id == "carrierSpread" || id == "modSpread"
                         || id == "carrierQ" || id == "modQ";

        if (layout && getParameter (id) != value)
            dirty.store (true, std::memory_order_relaxed);

        SynthModule::setParameter (id, value);
    }

    void blockStart() override
    {
        // Also catch index-based writes (setParameterAt), which bypass the
        // setParameter override: compare the layout values with the last build.
        float now[7];
        const int ids[7] = { pBands, pCarrierCentre, pModCentre, pCarrierSpread, pModSpread, pCarrierQ, pModQ };
        bool changed = false;
        for (int i = 0; i < 7; ++i)
        {
            now[i] = param (ids[i]);
            changed = changed || now[i] != builtLayout[i];
        }

        if (dirty.exchange (false, std::memory_order_relaxed) || changed)
        {
            std::copy (now, now + 7, builtLayout);
            rebuildBank();
        }
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

private:
    void rebuildBank();

    std::atomic<bool> dirty { true };
    int builtBands { -1 };
    float builtLayout[7] { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
    int numBands { 16 };
    float bandGain [kMaxBands] {};
    Biquad modFilters  [kMaxBands][kOrder];
    Biquad carFiltersL [kMaxBands][kOrder];
    Biquad carFiltersR [kMaxBands][kOrder];
    float envelope [kMaxBands] {};
};
