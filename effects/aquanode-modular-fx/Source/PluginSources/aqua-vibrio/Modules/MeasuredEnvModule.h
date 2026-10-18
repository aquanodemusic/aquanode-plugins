#pragma once
//==============================================================================
//  MeasuredEnvModule.h
//
//  The Original engine's envelope, shaped after measurements of a reference
//  instrument rather than designed by ear:
//
//    Attack   a straight line in amplitude, from wherever the level is to 1.
//    Decay    exponential - a straight line in dB - down to the sustain level,
//             where it stops (it does not slow on the way in).
//    Sustain  then drifts at a constant rate in dB (Sustain Time): falling
//             below the centre of the knob, rising (to full level) above it.
//    Release  exponential again, at its own rate in dB per second.
//
//  So unlike hardwareEnvModule, the parameters are not stage times:
//      attack       ms for the whole linear ramp
//      decay        dB per second
//      sustain      linear level 0..1
//      sustainTime  dB per second, signed (+ rises, - falls, 0 holds)
//      release      dB per second
//
//  The engine turns the patch bytes into these with the curves in
//  AquaVibrioOriginalDSP.h.
//
//  With "modulator" on (the filter envelope) the sustain is a plain level,
//  the decay falls towards zero and stops at it, and Sustain Time is a
//  straight-line slope in units per second - all as measured on the reference's
//  filter envelope driving Osc 2's pitch.
//
//  Output: 0 = Modulation Out, unipolar 0..1, per voice.
//==============================================================================

#include <JuceHeader.h>
#include "../Aquanode/ModuleCore.h"

namespace aquavibrio
{

class MeasuredEnvModule : public aquanode::SynthModule
{
public:
    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void processVoiceSample (int voice, const aquanode::StereoFrame*,
                             aquanode::StereoFrame* outputs) override
    {
        if (! resolved)
            resolve();

        auto& s = state[(size_t) voice];

        if (memoRate != sampleRate)
        {
            for (auto& st : state) { st.decayCoef.clear(); st.releaseCoef.clear(); st.slopeCoef.clear(); }
            memoRate = sampleRate;
        }

        // dB per second -> per-sample multiplier
        const auto perSample = [this] (float dbPerSecond)
        {
            return (float) std::pow (10.0, -(double) dbPerSecond / (20.0 * sampleRate));
        };

        switch (s.stage)
        {
            case Stage::attack:
            {
                const float ms = juce::jmax (0.01f, param (pAttack));   // the shortest start at full level at once
                s.level += (float) (1.0 / (ms * 0.001 * sampleRate));
                if (s.level >= 1.0f) { s.level = 1.0f; s.stage = Stage::decay; }
                break;
            }

            case Stage::decay:
            {
                const float sustain = juce::jlimit (0.0f, 1.0f, param (pSustain));
                const float c = s.decayCoef.get (juce::jmax (0.01f, param (pDecay)), perSample);
                // A straight line in dB that stops dead at the sustain level
                // (measured on both envelopes: no slowing on the way in).
                s.level *= c;
                if (s.level <= sustain + 1.0e-6f) { s.level = sustain; s.stage = Stage::sustain; }
                break;
            }

            case Stage::sustain:
            {
                const float rate = param (pSustainTime);   // + rises, - falls
                if (modMode())
                {
                    // as a modulator the sustain slope is a straight line (rate in units per second)
                    s.level = juce::jlimit (0.0f, 1.0f, s.level + rate / (float) sampleRate);
                    break;
                }
                if (std::abs (rate) > 1.0e-4f)
                {
                    const float c = s.slopeCoef.get (-rate, perSample);   // >1 when rising
                    // a rising sustain tops out a little above full level (+1.3 dB), as measured
                    s.level = juce::jmin (1.16f, juce::jmax (1.0e-7f, s.level) * c);
                }
                else
                {
                    // follow the sustain knob live, gently
                    s.level += (juce::jlimit (0.0f, 1.0f, param (pSustain)) - s.level) * 0.002f;
                }
                break;
            }

            case Stage::release:
                s.level *= s.releaseCoef.get (juce::jmax (0.01f, param (pRelease)), perSample);
                if (s.level < 1.0e-4f) { s.level = 0.0f; s.stage = Stage::idle; }
                break;

            case Stage::idle:
            default:
                s.level = 0.0f;
                break;
        }

        outputs[0][0] = s.level;
        outputs[0][1] = s.level;
    }

    void voiceNoteOn (int voice, int, bool retrigger) override
    {
        auto& s = state[(size_t) voice];
        s.stage = Stage::attack;
        if (! retrigger)
            s.level = 0.0f;
    }

    void voiceNoteOff (int voice) override
    {
        auto& s = state[(size_t) voice];
        if (s.stage != Stage::idle)
            s.stage = Stage::release;
    }

    bool isIdle (int voice) const   { return state[(size_t) voice].stage == Stage::idle; }
    float level (int voice) const   { return state[(size_t) voice].level; }

    void voiceReset (int voice) override { state[(size_t) voice] = {}; }

    double voiceTailSeconds() const override
    {
        // time to fall 80 dB at the release rate
        return 80.0 / juce::jmax (1.0, (double) param (pRelease));
    }

private:
    enum class Stage { idle, attack, decay, sustain, release };

    struct VoiceState
    {
        Stage stage { Stage::idle };
        float level { 0.0f };
        aquanode::Memo<float, float> decayCoef, releaseCoef, slopeCoef;
    };

    void resolve()
    {
        pAttack = paramIndex ("attack");
        pDecay = paramIndex ("decay");
        pSustain = paramIndex ("sustain");
        pRelease = paramIndex ("release");
        pSustainTime = paramIndex ("sustainTime");
        pMod = paramIndex ("modulator");
        resolved = true;
    }

    std::array<VoiceState, aquanode::kMaxVoices> state {};
    bool modMode() const { return pMod >= 0 && param (pMod) > 0.5f; }
    int pAttack { -1 }, pDecay { -1 }, pSustain { -1 }, pRelease { -1 }, pSustainTime { -1 }, pMod { -1 };
    bool resolved { false };
    double memoRate { 0.0 };
};

} // namespace aquavibrio
