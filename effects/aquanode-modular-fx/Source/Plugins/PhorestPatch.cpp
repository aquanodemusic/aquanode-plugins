//==============================================================================
// Phorest - Patch version
//
// Phorest is one effect - up to 48 first-order allpass stages swept together
// by an LFO, with feedback around them - so its patch version is one module,
// "Phorest - Phaser", running Phorest's own DSP sample by sample: the seven
// LFO shapes (or Manual), the depth window, the right channel's LFO offset,
// the log-spaced stages with their alternating detune, the feedback, the
// dry / wet mix and the Dry Cancel. Every knob takes cables; LFO Out is the
// left channel's sweep position (0..1) for anything else to follow.
//==============================================================================
#include "PatchKit.h"

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "phorest" };
}

//==============================================================================
class PhorestPhaserModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "plugin.phorest.phaser";
        d.displayName = kit::moduleName (kPlugin, "Phaser");
        d.description = "Phorest's phaser: up to 48 allpass stages spread up from the sweep position (log "
                        "spaced, alternately detuned), swept by its LFO between Min and Max (or held at "
                        "Position when the shape is Manual) from 20 Hz up to Range, with feedback around the "
                        "stages. Stereo offsets the right channel's LFO; Cancel takes some of the dry signal "
                        "out again. LFO Out is the sweep position, 0..1.";
        d.section = ModuleSection::Effect;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out"), modOut ("lfoOut", "LFO Out") };
        d.params = {
            makeCombo ("lfoShape", "LFO Shape", { "Manual", "Sine", "Triangle", "Ramp Up", "Ramp Down", "Square", "Tanh" }, 1, 0, 2),
            makeRotary ("sweepFreq", "Rate", 0.01f, 100.0f, 0.25f, 0, "Hz", true),
            makeRotary ("manualPos", "Position", 0.0f, 1.0f, 0.5f, 0).visibleWhen ("lfoShape", 0.0f),
            makeRotary ("minDepth", "Min", 0.0f, 1.0f, 0.0f, 0),
            makeRotary ("maxDepth", "Max", 0.0f, 1.0f, 1.0f, 0),
            makeRotary ("stereo", "Stereo", 0.0f, 1.0f, 0.25f, 0),

            makeRotary ("freqRange", "Range", 50.0f, 20000.0f, 1500.0f, 1, "Hz", true),
            makeRotary ("stages", "Stages", 4.0f, 48.0f, 20.0f, 1, {}, false, 1.0f),
            makeRotary ("feedback", "Feedback", 0.0f, 0.99f, 0.9f, 1),
            makeRotary ("detune", "Detune", 0.0f, 1.0f, 0.5f, 1),
            makeRotary ("dryWet", "Dry-Wet", 0.0f, 1.0f, 1.0f, 1),
            makeRotary ("dryCancel", "Cancel", 0.0f, 1.0f, 0.15f, 1),
            makeRotary ("outGain", "Out Gain", -24.0f, 24.0f, 0.0f, 1, "dB")
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
    }

    void reset() override
    {
        left = {};
        right = {};
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const float sweepFreq = param (1), manualPos = param (2), minDepth = param (3), maxDepth = param (4);
        const float stereoPhase = param (5), freqRange = param (6), feedback = param (8), detune = param (9);
        const float dryWet = param (10), dryCancel = param (11);
        const int lfoShape = (int) std::lround (param (0));
        const int stages = juce::jlimit (4, 48, (int) std::lround (param (7)));
        const float outGain = juce::Decibels::decibelsToGain (param (12));
        constexpr float twoPi = juce::MathConstants<float>::twoPi;

        float leftLfo, rightLfo;
        if (lfoShape == 0)
        {
            leftLfo = rightLfo = manualPos;
        }
        else
        {
            left.lfoPhase += (sweepFreq / (float) sampleRate) * twoPi;
            if (left.lfoPhase >= twoPi)
                left.lfoPhase -= twoPi;
            float rightPhase = left.lfoPhase + stereoPhase * twoPi;
            if (rightPhase >= twoPi)
                rightPhase -= twoPi;
            leftLfo  = juce::jlimit (0.0f, 1.0f, minDepth + lfo (left.lfoPhase, lfoShape) * (maxDepth - minDepth));
            rightLfo = juce::jlimit (0.0f, 1.0f, minDepth + lfo (rightPhase, lfoShape) * (maxDepth - minDepth));
        }

        const float xl = in[0][0], xr = in[0][1];
        const float wl = phase (xl, left, leftLfo, stages, feedback, detune, freqRange);
        const float wr = phase (xr, right, rightLfo, stages, feedback, detune, freqRange);
        out[0] = { (xl * (1.0f - dryWet) + wl * dryWet - xl * dryCancel) * outGain,
                   (xr * (1.0f - dryWet) + wr * dryWet - xr * dryCancel) * outGain };
        out[1] = { leftLfo, leftLfo };
    }

private:
    struct State
    {
        std::array<float, 48> z1 {};
        float lfoPhase { 0.0f };
        float feedbackSample { 0.0f };
    };

    // Phorest's generateLFO: 0..1
    static float lfo (float phase, int shape)
    {
        constexpr float pi = juce::MathConstants<float>::pi;
        float v;
        switch (shape)
        {
            case 2:  v = (2.0f / pi) * std::asin (std::sin (phase)); break;
            case 3:  v = (2.0f / pi) * (phase - pi) - 1.0f; if (v < -1.0f) v += 2.0f; break;
            case 4:  v = -(2.0f / pi) * (phase - pi) + 1.0f; if (v > 1.0f) v -= 2.0f; break;
            case 5:  v = std::sin (phase) >= 0.0f ? 1.0f : -1.0f; break;
            case 6:  v = std::tanh (2.0f * std::sin (phase)); break;
            default: v = std::sin (phase); break;
        }
        return v * 0.5f + 0.5f;
    }

    float allpassCoeff (float hz) const
    {
        hz = juce::jlimit (20.0f, (float) (sampleRate * 0.48f), hz);
        const float t = std::tan (juce::MathConstants<float>::pi * hz / (float) sampleRate);
        return (t - 1.0f) / (t + 1.0f);
    }

    // Phorest's processSample: the stages from 20 Hz * (Range / 20 Hz)^position up
    float phase (float input, State& s, float pos, int numStages, float feedback, float detune, float maxHz) const
    {
        constexpr float minHz = 20.0f;
        maxHz = juce::jlimit (minHz + 1.0f, (float) (sampleRate * 0.45f), maxHz);
        const float centre = minHz * std::pow (maxHz / minHz, juce::jlimit (0.0f, 1.0f, pos));

        float x = input + s.feedbackSample * feedback;
        for (int stage = 0; stage < numStages; ++stage)
        {
            float f = centre * std::pow (1.35f, (float) stage);
            if (detune > 0.0f)
                f *= 1.0f + detune * 0.12f * (stage % 2 == 0 ? 1.0f : -1.0f);
            f = juce::jlimit (10.0f, (float) (sampleRate * 0.45f), f);
            const float a = allpassCoeff (f);
            const float y = a * x + s.z1[(size_t) stage];
            s.z1[(size_t) stage] = x - a * y;
            x = y;
        }
        s.feedbackSample = x;
        return x;
    }

    State left, right;
};

AQUANODE_REGISTER_MODULE (PhorestPhaserModule, PhorestPhaserModule::descriptor)

//==============================================================================
namespace aquanode::plugins::phorest
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        const int phaser = b.add ("plugin.phorest.phaser", 0, 0);
        b.copy (phaser, original);

        PatchPorts ports;
        ports.name = "Phorest";
        ports.audioIn = { { phaser, "audioIn" } };
        ports.audioOut = { { phaser, "audioOut" } };
        return ports;
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, nullptr);
}
