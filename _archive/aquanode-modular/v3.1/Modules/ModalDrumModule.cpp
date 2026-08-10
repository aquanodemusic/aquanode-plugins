#include "ModalDrumModule.h"

using namespace aquanode;

void ModalDrumModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    // voice-steal de-click: a muted voice ramps out over a few ms instead of
    // being cut dead, and a re-used voice ramps back in
    const float voiceGain = pool.nextGain (v, sampleRate);
    if (pool.isSilent (v))
    {
        outputs[0][0] = 0.0f;
        outputs[0][1] = 0.0f;
        return;
    }

    renderVoice (v, inputs, outputs);

    outputs[0][0] *= voiceGain;
    outputs[0][1] *= voiceGain;
}

void ModalDrumModule::renderVoice (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const float trigIn = inputs[0][0];
    if (trigIn > 0.5f && lastTrig[v] <= 0.5f)
        trigger (v);
    lastTrig[v] = trigIn;

    // exciter: short noise burst with a linear fade-out
    float excite = 0.0f;
    if (exciteSamples[v] > 0)
    {
        --exciteSamples[v];
        excite = (random.nextFloat() * 2.0f - 1.0f) * 0.6f;
    }

    const float semis = noteOffset[v] + inputs[1][0] * 60.0f;   // Pitch In, KeyTrack scaling
    const double f0 = juce::jlimit (25.0, 4000.0,
        (double) param (pTune) * std::pow (2.0, (double) semis / 12.0));

    const float inharm = param (pInharm) * 0.01f;
    const float bright = param (pBright) * 0.01f;
    const float decaySec = juce::jmax (0.01f, param (pDecay) * 0.001f);
    const double nyqLimit = sampleRate * 0.45;

    float sum = 0.0f;

    for (int m = 0; m < kNumModes; ++m)
    {
        // partial ratios: harmonic (1,2,3..) stretched toward bell-like as Inharm rises
        const double ratio = std::pow ((double) (m + 1), 1.0 + 0.6 * inharm);
        const double freq = f0 * ratio;
        if (freq >= nyqLimit)
            continue;

        // higher modes decay faster (frequency-dependent damping)
        const float modeDecay = decaySec / (1.0f + 0.7f * (float) m);
        const float r = std::exp ((float) (-1.0 / (modeDecay * sampleRate)));

        const double theta = juce::MathConstants<double>::twoPi * freq / sampleRate;
        const float c = 2.0f * r * (float) std::cos (theta);
        const float rr = r * r;

        // mode amplitude: 1/(m+1) tilt, Bright lifts the upper modes
        const float amp = (0.3f + 0.7f * bright * (float) m / (kNumModes - 1)) / (float) (1 + m * (1.0f - bright));

        const float y = c * y1[v][m] - rr * y2[v][m] + excite * amp;
        y2[v][m] = y1[v][m];
        y1[v][m] = y;
        sum += y;
    }

    const float out = std::tanh (sum * 0.8f);
    outputs[0][0] = out;
    outputs[0][1] = out;
}

static ModuleDescriptor modalDrumDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.modaldrum";
    d.displayName = "Modal Drum";
    d.description =
        "Physical-modelling percussion: a short exciter feeding six parallel resonators, with "
        "Inharm morphing the partials from harmonic (marimba, tom) to stretched (bells, metal). "
        "Trig In takes a Clock, Euclid or Step Seq gate; Pitch In takes a Step Seq's Pitch Out or "
        "KeyTrack (semitones/60) - Step Seq melodies on toms.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 5;
    d.sockets = {
        modIn    ("trigIn",   "Trig In"),
        modIn    ("pitchIn",  "Pitch In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("tune",   "Tune",   30.0f, 1000.0f, 120.0f, 0, "Hz", true),
        makeRotary ("decay",  "Decay",  20.0f, 5000.0f, 600.0f, 0, "ms", true),
        makeRotary ("inharm", "Inharm", 0.0f, 100.0f, 20.0f, 0, "%"),
        makeRotary ("bright", "Bright", 0.0f, 100.0f, 40.0f, 0, "%"),
        makeRotary ("hit",    "Hit",    0.0f, 100.0f, 30.0f, 0, "%")
    ,
        makeRotary ("voices", "Voices", 1.0f, (float) kMaxVoices, (float) kMaxVoices, 1, {}, false, 1.0f).noMod()
    };
    return d;
}

AQUANODE_REGISTER_MODULE (ModalDrumModule, modalDrumDescriptor)
