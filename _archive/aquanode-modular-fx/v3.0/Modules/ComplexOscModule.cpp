#include "ComplexOscModule.h"

using namespace aquanode;

void ComplexOscModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
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

void ComplexOscModule::renderVoice (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const double twoPi = juce::MathConstants<double>::twoPi;
    const double freq = midiNoteToHz ((double) glide.next (v, glideMillis(), isMonoVoice(),
                                                           ! pool.isMuted (v), sampleRate));

    // internal modulation oscillator (the 259's second oscillator)
    modPhase[v] += freq * param (pModRatio) / sampleRate;
    modPhase[v] -= std::floor (modPhase[v]);
    const double modOsc = std::sin (modPhase[v] * twoPi);

    // principal oscillator, phase-modulated by the mod osc and by FM In
    phase[v] += freq / sampleRate;
    phase[v] -= std::floor (phase[v]);

    double readPhase = phase[v] + modOsc * param (pModIndex) + (double) inputs[0][0];
    readPhase -= std::floor (readPhase);
    float x = (float) std::sin (readPhase * twoPi);

    // the wavefolder: drive it harder and the sine folds into more harmonics
    const float timbre = juce::jlimit (0.0f, 1.0f,
        param (pTimbre) * 0.01f + param (pTimbreMod) * 0.01f * inputs[1][0]);

    x = x * (1.0f + timbre * 6.0f) + param (pSymmetry);
    x = fold (x, (int) param (pFolds));

    const bool envConnected = isInputConnected (2);
    const float gain = gate.next (v, envConnected, inputs[2][0], sampleRate);

    const float out = x * gain * param (pVolume);
    outputs[0][0] = out;
    outputs[0][1] = out;
}

static ModuleDescriptor complexOscDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.complex";
    d.displayName = "Complex Osc";
    d.description =
        "A modulation oscillator FMs the principal oscillator, "
        "and the result is pushed through a wavefolder. Where a filter removes harmonics, folding "
        "ADDS them, which is why Timbre sounds nothing like a cutoff sweep. Timbre In wants an "
        "LFO or ADSR; Env In wants an ADSR; non-integer Mod Ratios give the clangorous Buchla "
        "bell.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 11;
    d.sockets = {
        modIn    ("fmIn",      "FM In"),
        modIn    ("timbreIn",  "Timbre In"),
        modIn    ("envIn",     "Env In"),
        midiIn   ("addMidiIn", "Add Midi In"),
        audioOut ("audioOut",  "Audio Out")
    };
    d.params = {
        makeRotary ("volume",    "Volume",    0.0f, 1.0f, 0.7f, 0),
        makeRotary ("timbre",    "Timbre",    0.0f, 100.0f, 30.0f, 0, "%"),
        makeRotary ("symmetry",  "Symmetry",  -1.0f, 1.0f, 0.0f, 0),
        makeRotary ("folds",     "Folds",     1.0f, 4.0f, 2.0f, 1, {}, false, 1.0f),
        makeRotary ("modRatio",  "Mod Ratio", 0.25f, 8.0f, 1.5f, 1, {}, true),
        makeRotary ("modIndex",  "Mod Index", 0.0f, 4.0f, 0.0f, 1),
        makeRotary ("timbreMod", "Timb Mod",  -100.0f, 100.0f, 0.0f, 2, "%"),
        makeRotary ("voices",    "Voices",    1.0f, (float) kMaxVoices, (float) kMaxVoices, 2, {}, false, 1.0f).noMod(),
        makeRotary ("glide",     "Glide",     0.0f, 1000.0f, 0.0f, 2, "ms").visibleWhen ("voices", 1.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (ComplexOscModule, complexOscDescriptor)
