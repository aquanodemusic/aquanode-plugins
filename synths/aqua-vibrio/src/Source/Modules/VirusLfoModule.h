#pragma once
//==============================================================================
//  VirusLfoModule.h
//
//  The third module written for this synth. `util.lfo` covers the first five
//  Virus shapes and nothing else, and four of the hardware's LFO controls
//  have no equivalent in it at all:
//
//    Shape        68 shapes, not 5 - past S&G the list runs into the same
//                 wave table the oscillators read
//    Symmetry     skews the cycle itself rather than the output, so a
//                 triangle becomes a ramp instead of a squashed triangle
//    Mode         Poly gives every voice its own phase, Mono shares one
//    Keytrigger   whether a new note restarts the cycle, and at what phase
//    Env Mode     one-shot: the LFO runs a single cycle and stops, which is
//                 how the Virus uses an LFO as an extra envelope
//
//  Output: 0 = Mod Out, bipolar -1..1, per voice.
//==============================================================================

#include <JuceHeader.h>
#include "../Aquanode/ModuleCore.h"
#include "VirusOscModule.h"

namespace aquavibrio
{

class VirusLfoModule : public aquanode::SynthModule
{
public:
    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void processVoiceSample (int voice, const aquanode::StereoFrame*,
                             aquanode::StereoFrame* outputs) override
    {
        if (! resolved)
            resolve();

        auto& s = state[(size_t) voice];

        const float rate = juce::jmax (0.001f, param (pRate));
        const int shape = juce::jlimit (0, 67, (int) param (pShape));
        const float symmetry = juce::jlimit (-0.95f, 0.95f, param (pSymmetry));
        const bool mono = param (pMode) > 0.5f;
        const bool envMode = param (pEnvMode) > 0.5f;

        // In Mono every voice reads one shared phase, so the whole chord
        // moves together instead of each note drifting on its own.
        // A mono LFO has one phase shared by every voice. It must advance once
        // per SAMPLE, not once per voice, or it runs N times too fast with N
        // voices sounding - so the engine calls advanceShared() for it.
        double& phase = mono ? sharedPhase : s.phase;

        const double increment = (double) rate / sampleRate;

        if (! mono && ! (envMode && s.finished))
        {
            phase += increment;

            if (phase >= 1.0)
            {
                if (envMode)
                {
                    phase = 1.0;
                    s.finished = true;      // one shot: stop at the end
                }
                else
                {
                    phase -= std::floor (phase);
                }
            }
        }

        // Symmetry stretches the first half of the cycle at the expense of
        // the second. Done on the phase, so the waveform's own shape changes
        // - a triangle really does become a ramp.
        const double warped = symmetry == 0.0f ? phase : skewPhase (phase, symmetry);

        float value = 0.0f;

        switch (shape)
        {
            case 0:  value = (float) std::sin (warped * juce::MathConstants<double>::twoPi); break;
            case 1:  value = (float) (warped < 0.5 ? 4.0 * warped - 1.0 : 3.0 - 4.0 * warped); break;
            case 2:  value = (float) (2.0 * warped - 1.0); break;
            case 3:  value = warped < 0.5 ? 1.0f : -1.0f; break;

            case 4:  // Sample & Hold: a new random level each cycle
                if (warped < s.lastPhase)
                    s.held = rng.nextFloat() * 2.0f - 1.0f;
                value = s.held;
                break;

            case 5:  // Sample & Glide: the same, smoothed
                if (warped < s.lastPhase)
                    s.target = rng.nextFloat() * 2.0f - 1.0f;
                s.held += (s.target - s.held) * (float) (increment * 8.0);
                value = s.held;
                break;

            default: // Waves 3..64 - the oscillators' table, read as an LFO
                value = VirusWaveTable::instance().read (juce::jlimit (0, 63, shape - 4),
                                                         warped, 1.0, 8192.0);
                break;
        }

        s.lastPhase = warped;

        outputs[0][0] = value;
        outputs[0][1] = value;
    }

    void voiceNoteOn (int voice, int, bool retrigger) override
    {
        auto& s = state[(size_t) voice];
        s.finished = false;

        if (! resolved)
            resolve();

        // Keytrigger off means the LFO free-runs and a new note lands
        // wherever the cycle happens to be. On, it restarts - at the phase
        // the Keytrigger value asks for, which is how a patch gets an LFO
        // that always begins at the top of its sweep.
        const float keytrigger = param (pKeytrigger);

        if (keytrigger >= 0.0f && ! retrigger)
            s.phase = (double) keytrigger;

        // Unison LFO Phase fans the voices out across the cycle so a unison
        // stack shimmers instead of pulsing as one.
        const float fan = param (pPhaseSpread);

        if (fan > 0.0f)
            s.phase = std::fmod (s.phase + (double) (fan * (float) voice) / 16.0, 1.0);
    }

    void voiceReset (int voice) override { state[(size_t) voice] = {}; }

    void reset() override
    {
        rng.setSeed (0x1f0);   // repeatable sample & hold after every reset
        sharedPhase = 0.0;
    }

    // Once per sample, from the engine: moves the shared (Mono mode) phase.
    void advanceShared()
    {
        if (! resolved)
            resolve();

        sharedPhase += (double) juce::jmax (0.001f, param (pRate)) / sampleRate;
        if (sharedPhase >= 1.0)
            sharedPhase -= std::floor (sharedPhase);
    }

private:
    // Piecewise: the first `symmetry`-weighted portion of the cycle is
    // stretched to fill the first half of the waveform, the rest squeezed
    // into the second.
    static double skewPhase (double phase, float symmetry)
    {
        const double pivot = juce::jlimit (0.02, 0.98, 0.5 + 0.5 * (double) symmetry);

        return phase < pivot ? 0.5 * phase / pivot
                             : 0.5 + 0.5 * (phase - pivot) / (1.0 - pivot);
    }

    struct VoiceState
    {
        double phase { 0.0 }, lastPhase { 0.0 };
        float held { 0.0f }, target { 0.0f };
        bool finished { false };
    };

    void resolve()
    {
        pRate = paramIndex ("rate");
        pShape = paramIndex ("shape");
        pSymmetry = paramIndex ("symmetry");
        pMode = paramIndex ("mode");
        pEnvMode = paramIndex ("envMode");
        pKeytrigger = paramIndex ("keytrigger");
        pPhaseSpread = paramIndex ("phaseSpread");
        resolved = true;
    }

    std::array<VoiceState, aquanode::kMaxVoices> state {};
    double sharedPhase { 0.0 };
    juce::Random rng;

    int pRate { -1 }, pShape { -1 }, pSymmetry { -1 }, pMode { -1 },
        pEnvMode { -1 }, pKeytrigger { -1 }, pPhaseSpread { -1 };
    bool resolved { false };
};

} // namespace aquavibrio
