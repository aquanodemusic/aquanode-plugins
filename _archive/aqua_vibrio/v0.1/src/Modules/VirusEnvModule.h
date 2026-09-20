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

        const auto rate = [this] (float ms) { return (float) (1.0 / (ms * 0.001 * sampleRate)); };

        switch (s.stage)
        {
            case Stage::attack:
                s.level += rate (attackMs);
                if (s.level >= 1.0f) { s.level = 1.0f; s.stage = Stage::decay; }
                break;

            case Stage::decay:
                s.level -= rate (decayMs) * (1.0f - sustain);
                if (s.level <= sustain) { s.level = sustain; s.stage = Stage::sustain; }
                break;

            case Stage::sustain:
                // The slope is deliberately slow: full deflection takes about
                // twenty seconds, which is the range the hardware covers and
                // the only range in which it is musical rather than a second
                // decay stage.
                if (sustainTime != 0.0f)
                {
                    s.level += sustainTime * (float) (1.0 / (20.0 * sampleRate));
                    s.level = juce::jlimit (0.0f, 1.0f, s.level);
                }
                break;

            case Stage::release:
                s.level -= rate (releaseMs) * juce::jmax (0.001f, s.releaseFrom);
                if (s.level <= 0.0f) { s.level = 0.0f; s.stage = Stage::idle; }
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
