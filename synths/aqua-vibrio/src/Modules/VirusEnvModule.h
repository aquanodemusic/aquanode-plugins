#pragma once
//==============================================================================
//  VirusEnvModule.h
//
//  An ADSR whose sustain is not flat. The Virus gives every envelope a
//  Sustain Time control running from Fall through Infinite to Rise: at the
//  centre the sustain holds as an ADSR's does, to the left it keeps decaying
//  towards zero while the key is held, to the right it climbs back up. Pads
//  that bloom and plucks that keep dying away both come from this one knob,
//  and `util.adsr` has no equivalent.
//
//  Output: 0 = Modulation Out, unipolar 0..1, per voice.
//==============================================================================

#include <JuceHeader.h>
#include "../Aquanode/ModuleCore.h"

namespace aquavibrio
{

class VirusEnvModule : public aquanode::SynthModule
{
public:
    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void processVoiceSample (int voice, const aquanode::StereoFrame*,
                             aquanode::StereoFrame* outputs) override
    {
        if (! resolved)
            resolve();

        auto& s = state[(size_t) voice];

        const float attackMs = juce::jmax (0.1f, param (pAttack));
        const float decayMs = juce::jmax (0.1f, param (pDecay));
        const float sustain = juce::jlimit (0.0f, 1.0f, param (pSustain));
        const float releaseMs = juce::jmax (0.1f, param (pRelease));
        const float sustainTime = param (pSustainTime);   // -1 fall .. 0 hold .. +1 rise

        // Attack rises linearly, the way an analogue attack sounds. Decay and
        // release are exponential - a time constant chosen so the stage is
        // about 99% complete after the set time - which is what makes a
        // pluck die away naturally instead of stopping like a ramp.
        const auto linRate = [this] (float ms) { return (float) (1.0 / (ms * 0.001 * sampleRate)); };
        const auto expCoef = [this] (float ms)
        {
            return (float) std::exp (-4.6 / (juce::jmax (0.05, (double) ms) * 0.001 * sampleRate));
        };

        switch (s.stage)
        {
            case Stage::attack:
                s.level += linRate (attackMs);
                if (s.level >= 1.0f) { s.level = 1.0f; s.stage = Stage::decay; }
                break;

            case Stage::decay:
            {
                const float c = expCoef (decayMs);
                s.level = sustain + (s.level - sustain) * c;
                if (std::abs (s.level - sustain) < 1.0e-4f) { s.level = sustain; s.stage = Stage::sustain; }
                break;
            }

            case Stage::sustain:
                // Sustain Time: 0 holds, towards -1 the level keeps falling,
                // towards +1 it climbs back up. The further from the centre,
                // the faster: about 30 s end to end just off centre, 20 ms at
                // the extremes.
                if (std::abs (sustainTime) > 0.01f)
                {
                    const float a = std::abs (sustainTime);
                    const float traverseSeconds = 30.0f * (1.0f - a) * (1.0f - a) * (1.0f - a) + 0.02f;
                    const float step = (float) (1.0 / (traverseSeconds * sampleRate));
                    s.level = juce::jlimit (0.0f, 1.0f, s.level + (sustainTime > 0.0f ? step : -step));
                }
                else
                {
                    // follow the sustain knob live, gently, so turning it
                    // while a note is held is audible and click free
                    s.level += (sustain - s.level) * 0.002f;
                }
                break;

            case Stage::release:
                s.level *= expCoef (releaseMs);
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

        // A retrigger picks up from where the envelope is rather than
        // restarting at zero, which is what stops repeated notes clicking.
        if (! retrigger)
            s.level = 0.0f;
    }

    void voiceNoteOff (int voice) override
    {
        auto& s = state[(size_t) voice];
        s.releaseFrom = juce::jmax (0.001f, s.level);
        s.stage = Stage::release;
    }

    bool isIdle (int voice) const   { return state[(size_t) voice].stage == Stage::idle; }
    float level (int voice) const   { return state[(size_t) voice].level; }

    void voiceReset (int voice) override
    {
        state[(size_t) voice] = {};
    }

    double voiceTailSeconds() const override
    {
        return (double) juce::jmax (0.1f, param (pRelease)) * 0.001;
    }

private:
    enum class Stage { idle, attack, decay, sustain, release };

    struct VoiceState
    {
        Stage stage { Stage::idle };
        float level { 0.0f };
        float releaseFrom { 1.0f };
    };

    void resolve()
    {
        pAttack = paramIndex ("attack");
        pDecay = paramIndex ("decay");
        pSustain = paramIndex ("sustain");
        pRelease = paramIndex ("release");
        pSustainTime = paramIndex ("sustainTime");
        resolved = true;
    }

    std::array<VoiceState, aquanode::kMaxVoices> state {};
    int pAttack { -1 }, pDecay { -1 }, pSustain { -1 }, pRelease { -1 }, pSustainTime { -1 };
    bool resolved { false };
};

} // namespace aquavibrio
