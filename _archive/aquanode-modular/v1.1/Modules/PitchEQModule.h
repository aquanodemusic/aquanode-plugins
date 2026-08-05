#pragma once

#include "ModuleCore.h"

// Pitch EQ - adapted from the PitchControlEQ VST, reduced to its core idea:
// pick note classes on the little keyboard and every note in the Lo..Hi range
// that is NOT selected gets dampened (up to 2 cascaded narrow cuts per note),
// while selected notes can be boosted - the signal is musically "re-tuned"
// toward the chosen pitch classes. The original's global bell, chorus and
// MIDI mode are dropped. With no keys selected the module passes through.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class PitchEQModule : public aquanode::SynthModule
{
public:
    static constexpr int kMaxDampenStages = 2;
    static constexpr double kMaxDampenPerStage = -32.0;   // dB per biquad stage
    static constexpr int kTotalNotes = 128;
    enum ParamIndex { pDampen = 0, pWidth, pBoost, pRangeLo, pRangeHi, pGain };
    // hidden note-class toggles follow: nc0 (C) .. nc11 (B)

    // transposed direct form II biquad (double precision, from the original)
    struct Biquad
    {
        double b0 { 1.0 }, b1 {}, b2 {}, a1 {}, a2 {};
        struct State { double s1 {}, s2 {}; };

        inline double process (double x, State& s) const noexcept
        {
            const double y = b0 * x + s.s1;
            s.s1 = b1 * x - a1 * y + s.s2;
            s.s2 = b2 * x - a2 * y;
            return y;
        }
    };

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        dirty.store (true, std::memory_order_relaxed);
        reset();
    }

    void reset() override
    {
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < kTotalNotes; ++n)
            {
                for (int st = 0; st < kMaxDampenStages; ++st)
                    dampenStates[c][n][st] = {};
                boostStates[c][n] = {};
            }
    }

    void setParameter (const juce::String& id, float value) override
    {
        SynthModule::setParameter (id, value);
        dirty.store (true, std::memory_order_relaxed);
    }

    void blockStart() override
    {
        if (dirty.exchange (false, std::memory_order_relaxed))
            rebuildFilters();
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 46; }

private:
    void rebuildFilters();

    std::atomic<bool> dirty { true };

    Biquad dampenFilters [kTotalNotes][kMaxDampenStages];
    Biquad boostFilters  [kTotalNotes];
    Biquad::State dampenStates [2][kTotalNotes][kMaxDampenStages];
    Biquad::State boostStates  [2][kTotalNotes];
    int dampenStageCount [kTotalNotes] {};
    bool boostActive [kTotalNotes] {};
    bool anyActive { false };
    int loNote { 0 }, hiNote { 0 };
};
