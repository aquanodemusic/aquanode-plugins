#include "OscillatorModule.h"

using namespace aquanode;

//==============================================================================
void OscillatorModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    reset();
}

void OscillatorModule::reset()
{
    for (int v = 0; v < kMaxVoices; ++v)
        voiceReset (v);
}

void OscillatorModule::voiceReset (int v)
{
    pool.resetVoice (v);
    glide.resetVoice (v);
    phase[v] = 0.0;
    freqHz[v] = 0.0;
    gateLvl[v] = 0.0f;
    gateOn[v] = false;
    vel[v] = 1.0f;
}

void OscillatorModule::blockStart()
{
    latchedSample = getLoadedSample();
    if (latchedSample != nullptr && latchedSample->getNumSamples() > 0)
    {
        tableData = latchedSample->getReadPointer (0);
        tableSourceLen = latchedSample->getNumSamples();
    }
    else
    {
        tableData = nullptr;
        tableSourceLen = 0;
    }
}

void OscillatorModule::voiceNoteOn (int v, int note, bool retrigger)
{
    pool.noteOn (v, voiceLimit());
    glide.noteOn (v, (float) note, isMonoVoice());
    gateOn[v] = true;
    if (! retrigger)
    {
        phase[v] = 0.0;
        gateLvl[v] = 0.0f;
    }
}

void OscillatorModule::voiceNoteOff (int v)
{
    pool.noteOff (v, voiceLimit());
    gateOn[v] = false;
}

float OscillatorModule::readWave (int waveform, double phase01) const
{
    switch (waveform)
    {
        case 0: return (float) std::sin (phase01 * juce::MathConstants<double>::twoPi);   // Sine
        case 1: return (float) (1.0 - 4.0 * std::abs (phase01 - 0.5));                    // Tri
        case 2: return (float) (2.0 * phase01 - 1.0);                                     // Saw
        case 3: return phase01 < 0.5 ? 1.0f : -1.0f;                                      // Square
        case 4: return readSampleTable (phase01);                                         // Sample
        default: return 0.0f;
    }
}

float OscillatorModule::readSampleTable (double phase01) const
{
    if (tableData == nullptr)
        return 0.0f;

    const int cycleLen = juce::jlimit (1, 2048, (int) param (pCycleLength));
    const double pos = phase01 * cycleLen;
    const int i0 = (int) pos;
    const int i1 = (i0 + 1) % cycleLen;
    const double frac = pos - i0;

    // only the first cycleLen samples form the wavetable; zero-pad if shorter
    const float s0 = i0 < tableSourceLen ? tableData[i0] : 0.0f;
    const float s1 = i1 < tableSourceLen ? tableData[i1] : 0.0f;
    return (float) (s0 + (s1 - s0) * frac);
}

void OscillatorModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
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

void OscillatorModule::renderVoice (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    // mono glide: slides from the previous note's pitch (no-op when polyphonic)
    freqHz[v] = midiNoteToHz ((double) glide.next (v, glideMillis(), isMonoVoice(), ! pool.isMuted (v), sampleRate));

    const float fmIn = inputs[0][0];               // this voice's own FM signal
    const bool envConnected = isInputConnected (1);
    const float envIn = envConnected ? inputs[1][0] : 1.0f;

    const double invSr = 1.0 / sampleRate;

    // carrier phase advance
    phase[v] += freqHz[v] * param (pFmRatio) * invSr;
    phase[v] -= std::floor (phase[v]);

    // true phase modulation: FM-In offsets the read phase by (fmIn * 2*pi)
    // radians == fmIn cycles, DX7-style. FM depth = the amplitude of whatever
    // feeds FM In - and since that signal is per-voice, chords don't stack
    // modulation depth.
    double readPhase = phase[v] + (double) fmIn;
    readPhase -= std::floor (readPhase);

    const float y = readWave ((int) param (pWaveform), readPhase);

    float gain;
    if (envConnected)
    {
        // amplitude comes entirely from this voice's envelope signal
        gain = envIn;
    }
    else
    {
        // unpatched Env In: simple on/off gate with a ~3ms anti-click ramp
        const float gateStep = (float) (invSr / gateRampSeconds);
        const float target = gateOn[v] ? 1.0f : 0.0f;
        if (gateLvl[v] < target)      gateLvl[v] = juce::jmin (target, gateLvl[v] + gateStep);
        else if (gateLvl[v] > target) gateLvl[v] = juce::jmax (target, gateLvl[v] - gateStep);
        gain = gateLvl[v];
    }

    const float out = y * gain * vel[v] * param (pVolume);   // velocity always applies
    outputs[0][0] = out;
    outputs[0][1] = out;
}

//==============================================================================
static ModuleDescriptor oscillatorDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.basic";
    d.displayName = "Oscillator";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 0;
    d.sockets = {
        modIn ("fmIn",  "FM In"),
        modIn ("envIn", "Env In"),
        midiIn ("addMidiIn", "Add Midi In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("volume",  "Volume",   0.0f, 1.0f, 0.8f, 0),
        makeRotary ("fmRatio", "FM Ratio", 0.001f, 100.0f, 1.0f, 0, {}, true),
        makeCombo  ("waveform", "Waveform", { "Sine", "Tri", "Saw", "Square", "Sample" }, 0, 0, 2),
        makeRotary ("cycleLength", "Cycle Length", 1.0f, 2048.0f, 2048.0f, 1, {}, false, 1.0f)
            .visibleWhen ("waveform", 4.0f),
        makeButton ("loadSample", "Load Sample", 1, 2)
            .visibleWhen ("waveform", 4.0f)
    ,
        makeRotary ("voices", "Voices", 1.0f, (float) kMaxVoices, (float) kMaxVoices, 3, {}, false, 1.0f).noMod(),
        makeRotary ("glide", "Glide", 0.0f, 1000.0f, 0.0f, 3, "ms").visibleWhen ("voices", 1.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (OscillatorModule, oscillatorDescriptor)
