#include "OscillatorModule.h"

using namespace aquanode;

//==============================================================================
void OscillatorModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    voices.reset();
    for (auto& p : phase) p = 0.0;
    for (auto& g : gateLvl) g = 0.0f;
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

void OscillatorModule::handleMidiEvent (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        bool retrigger = false;
        const int v = voices.noteOn (m.getNoteNumber(), retrigger);
        freqHz[v] = midiNoteToHz (m.getNoteNumber());
        releaseElapsed[v] = 0.0;
        if (! retrigger)
        {
            phase[v] = 0.0;
            gateLvl[v] = 0.0f;
        }
    }
    else if (m.isNoteOff())
    {
        voices.noteOff (m.getNoteNumber());
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        voices.allGatesOff();
    }
}

float OscillatorModule::readWave (int waveform, double phase01) const
{
    switch (waveform)
    {
        case 0: return (float) std::sin (phase01 * juce::MathConstants<double>::twoPi);           // Sine
        case 1: return (float) (1.0 - 4.0 * std::abs (phase01 - 0.5));                            // Tri
        case 2: return (float) (2.0 * phase01 - 1.0);                                             // Saw
        case 3: return phase01 < 0.5 ? 1.0f : -1.0f;                                              // Square
        case 4: return readSampleTable (phase01);                                                 // Sample
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

void OscillatorModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    const float fmIn = inputs[0][0];               // Modulation, mono
    const bool envConnected = isInputConnected (1);
    const float envIn = envConnected ? inputs[1][0] : 1.0f;

    const float volume  = param (pVolume);
    const float fmRatio = param (pFmRatio);
    const int   wave    = (int) param (pWaveform);

    const double invSr = 1.0 / sampleRate;
    const float gateStep = (float) (invSr / gateRampSeconds);

    float sum = 0.0f;

    for (int v = 0; v < VoiceAllocator::maxVoices; ++v)
    {
        auto& slot = voices.slots[v];
        if (! slot.inUse)
            continue;

        // carrier phase advance
        phase[v] += freqHz[v] * fmRatio * invSr;
        phase[v] -= std::floor (phase[v]);

        // true phase modulation: FM-In offsets the read phase by
        // (fmIn * 2*pi) radians == fmIn cycles, DX7-style. FM depth is the
        // amplitude of whatever feeds FM In - no separate depth knob.
        double readPhase = phase[v] + (double) fmIn;
        readPhase -= std::floor (readPhase);

        const float y = readWave (wave, readPhase);

        float gain;
        if (envConnected)
        {
            // envelope shaping happens via the summed Env In signal;
            // keep the voice sounding through the external release tail
            gain = 1.0f;
            if (! slot.gate)
            {
                releaseElapsed[v] += invSr;
                if (releaseElapsed[v] > envTailSeconds)
                {
                    voices.freeVoice (v);
                    continue;
                }
            }
        }
        else
        {
            // unpatched Env In: simple on/off gate with a ~3ms anti-click ramp
            const float target = slot.gate ? 1.0f : 0.0f;
            if (gateLvl[v] < target)      gateLvl[v] = juce::jmin (target, gateLvl[v] + gateStep);
            else if (gateLvl[v] > target) gateLvl[v] = juce::jmax (target, gateLvl[v] - gateStep);

            if (! slot.gate && gateLvl[v] <= 0.0f)
            {
                voices.freeVoice (v);
                continue;
            }
            gain = gateLvl[v];
        }

        sum += y * gain;
    }

    const float out = sum * volume * envIn;
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
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("volume",  "Volume",   0.0f, 1.0f, 0.8f, 0),
        makeRotary ("fmRatio", "FM Ratio", 0.25f, 8.0f, 1.0f, 0),
        makeCombo  ("waveform", "Waveform", { "Sine", "Tri", "Saw", "Square", "Sample" }, 0, 0, 2),
        makeRotary ("cycleLength", "Cycle Length", 1.0f, 2048.0f, 2048.0f, 1, {}, false, 1.0f)
            .visibleWhen ("waveform", 4.0f),
        makeButton ("loadSample", "Load Sample", 1, 2)
            .visibleWhen ("waveform", 4.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (OscillatorModule, oscillatorDescriptor)
